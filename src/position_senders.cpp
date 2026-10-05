#include "polymarket/evm_transaction.hpp"
#include "evm_uint.hpp"
#include "polymarket/evm_utils.hpp"
#include "position_client_impl.hpp"
#include "transaction_waiters.hpp"
#include <thread>

namespace polymarket
{
    namespace
    {
        // 0x-hex quantity (no leading zeros) for a base-10 or hex uint256 string.
        std::string to_quantity(const std::string &value)
        {
            const auto word = detail::parse_uint256_word(value);
            const auto hex = to_hex(word).substr(2);
            const auto first = hex.find_first_not_of('0');
            return first == std::string::npos ? "0x0" : "0x" + hex.substr(first);
        }
    } // namespace

    PositionClient::Impl::Impl(PositionClientConfig &config)
        : contracts(std::move(config.contracts)),
          signer(config.private_key, static_cast<int>(contracts.chain_id)),
          signer_address(signer.address()),
          wallet_type(config.wallet_type),
          rpc(std::make_shared<EvmJsonRpcHttpClient>(config.rpc_url)),
          relayer_retry_delay(config.relayer_retry_delay_ms),
          relayer_max_submit_retries(config.relayer_max_submit_retries)
    {
        rpc->set_timeout_ms(config.rpc_timeout_ms);
        gamma.set_base_url(config.gamma_api_url);

        if (wallet_type == SignatureType::EOA)
        {
            wallet = signer_address;
        }
        else if (wallet_type == SignatureType::POLY_GNOSIS_SAFE)
        {
            wallet = detail::derive_safe_address(signer_address, contracts);
            if (config.relayer_api_key.empty())
                throw std::invalid_argument("POLY_GNOSIS_SAFE needs relayer_api_key");
            relayer = std::make_shared<detail::RelayerClient>(
                config.relayer_url, config.relayer_api_key,
                config.relayer_api_key_address.empty() ? signer_address : config.relayer_api_key_address);
        }
        else
        {
            throw std::invalid_argument("PositionClient supports EOA and POLY_GNOSIS_SAFE wallets");
        }
        if (!config.funder_address.empty() &&
            evm_normalize_hex(config.funder_address) != evm_normalize_hex(wallet))
            throw std::invalid_argument("funder_address " + config.funder_address + " is not the " +
                                        (wallet_type == SignatureType::EOA ? "signer address " : "Safe ") +
                                        wallet + " controlled by this key");
    }

    void PositionClient::Impl::ensure_chain()
    {
        if (chain_verified)
            return;
        const auto node_chain = rpc->chain_id();
        if (node_chain != contracts.chain_id)
            throw std::runtime_error("RPC node is on chain " + std::to_string(node_chain) + ", expected " +
                                     std::to_string(contracts.chain_id));
        chain_verified = true;
    }

    std::string PositionClient::Impl::send_from_eoa(const ContractCall &call)
    {
        ensure_chain();
        const auto &from = signer_address;
        const auto value = to_quantity(call.value);
        EvmLegacyTransaction tx;
        tx.nonce = rpc->get_transaction_count(from, "pending");
        tx.gas_price = rpc->gas_price();
        // Estimation also surfaces reverts (missing approval, no balance) before anything is sent.
        tx.gas_limit = rpc->estimate_gas({call.to, call.data, from, value});
        tx.to = call.to;
        tx.value = value;
        tx.data = call.data;
        tx.chain_id = contracts.chain_id;

        const auto signed_tx = evm_sign_legacy_transaction(signer, tx);
        const auto node_hash = rpc->send_raw_transaction(signed_tx.raw_transaction);
        if (evm_normalize_hex(node_hash) != signed_tx.transaction_hash)
            throw std::runtime_error("RPC node reported hash " + node_hash + " for transaction " +
                                     signed_tx.transaction_hash);
        return signed_tx.transaction_hash;
    }

    TransactionHandle PositionClient::Impl::execute_from_eoa(const std::vector<ContractCall> &calls)
    {
        std::lock_guard<std::mutex> lock(send_mutex);
        std::vector<std::string> hashes;
        for (size_t i = 0; i < calls.size(); ++i)
        {
            try
            {
                hashes.push_back(send_from_eoa(calls[i]));
                // Later calls may depend on earlier ones, so each must land first.
                if (i + 1 < calls.size())
                    (void)detail::wait_for_receipt(*rpc, hashes.back(), std::chrono::minutes(3),
                                                   std::chrono::seconds(2));
            }
            catch (const std::exception &error)
            {
                // Nothing on chain yet: the original error is the whole story.
                if (hashes.empty())
                    throw;
                throw PartialBatchError(hashes, i, calls.size(), std::current_exception(), error.what());
            }
        }
        auto rpc_client = rpc;
        auto last = hashes.back();
        return TransactionHandle(std::move(hashes), "",
                                 [rpc_client, last](std::chrono::milliseconds timeout, std::chrono::milliseconds poll)
                                 { return detail::wait_for_receipt(*rpc_client, last, timeout, poll); });
    }

    TransactionHandle PositionClient::Impl::execute_from_safe(const std::vector<ContractCall> &calls,
                                                                const std::string &metadata)
    {
        std::lock_guard<std::mutex> lock(send_mutex);
        ensure_chain();
        const auto safe_call = detail::resolve_safe_call(calls, contracts);
        detail::RelayerTransaction submitted;
        for (int attempt = 0;; ++attempt)
        {
            try
            {
                // A fresh nonce and signature per attempt, as a retry may follow a nonce change.
                const auto nonce = relayer->safe_nonce(signer_address);
                const auto signature = detail::sign_safe_digest(
                    signer, detail::safe_transaction_digest(wallet, contracts.chain_id, safe_call, nonce));
                submitted = relayer->submit(
                    detail::build_safe_payload(signer_address, wallet, safe_call, nonce, signature, metadata));
                break;
            }
            catch (const detail::RelayerRequestError &error)
            {
                if (attempt >= relayer_max_submit_retries || !detail::is_retryable_submit_error(error))
                    throw;
            }
            std::this_thread::sleep_for(relayer_retry_delay);
        }

        std::vector<std::string> hashes;
        if (!submitted.transaction_hash.empty())
            hashes.push_back(submitted.transaction_hash);
        auto relayer_client = relayer;
        auto rpc_client = rpc;
        const auto id = submitted.transaction_id;
        const auto submit_hash = submitted.transaction_hash;
        return TransactionHandle(std::move(hashes), id,
                                 [relayer_client, rpc_client, id, submit_hash](std::chrono::milliseconds timeout,
                                                                               std::chrono::milliseconds poll)
                                 { return detail::wait_for_relayer(*relayer_client, *rpc_client, id, submit_hash, timeout, poll); });
    }

} // namespace polymarket
