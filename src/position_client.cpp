#include "position_client.hpp"
#include "evm_transaction.hpp"
#include "evm_uint.hpp"
#include "evm_utils.hpp"
#include "http_client.hpp"
#include "order_signer.hpp"
#include "position_calls.hpp"
#include "safe_relayer.hpp"
#include <algorithm>
#include <mutex>
#include <set>
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

        TransactionOutcome wait_for_receipt(EvmJsonRpcHttpClient &rpc, const std::string &transaction_hash,
                                            std::chrono::milliseconds timeout,
                                            std::chrono::milliseconds poll_interval)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (true)
            {
                if (auto receipt = rpc.get_transaction_receipt(transaction_hash))
                {
                    if (!receipt->success)
                        throw TransactionRevertedError(transaction_hash, std::move(*receipt));
                    return {transaction_hash, "", std::move(*receipt)};
                }
                if (std::chrono::steady_clock::now() + poll_interval > deadline)
                    throw TransactionTimeoutError(transaction_hash);
                std::this_thread::sleep_for(poll_interval);
            }
        }

        std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline)
        {
            return std::max(std::chrono::milliseconds::zero(),
                            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        }

        // Polls the relayer until the transaction settles, then reads its receipt.
        TransactionOutcome wait_for_relayer(detail::RelayerClient &relayer, EvmJsonRpcHttpClient &rpc,
                                            const std::string &transaction_id, const std::string &submit_hash,
                                            std::chrono::milliseconds timeout, std::chrono::milliseconds poll_interval)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (true)
            {
                const auto tx = relayer.get_transaction(transaction_id);
                if (tx.state == "STATE_CONFIRMED")
                {
                    const auto hash = tx.transaction_hash.empty() ? submit_hash : tx.transaction_hash;
                    if (hash.empty())
                        throw std::runtime_error("relayer transaction " + transaction_id +
                                                 " settled without a transaction hash");
                    auto outcome = wait_for_receipt(rpc, hash, remaining(deadline), poll_interval);
                    outcome.transaction_id = transaction_id;
                    return outcome;
                }
                if (tx.state == "STATE_FAILED" || tx.state == "STATE_INVALID")
                    throw TransactionFailedError(transaction_id, tx.state, tx.error_message);
                if (std::chrono::steady_clock::now() + poll_interval > deadline)
                    throw TransactionTimeoutError(tx.transaction_hash.empty() ? transaction_id : tx.transaction_hash);
                std::this_thread::sleep_for(poll_interval);
            }
        }
    } // namespace

    TransactionRevertedError::TransactionRevertedError(std::string transaction_hash, EvmTransactionReceipt receipt)
        : std::runtime_error("transaction " + transaction_hash + " reverted"),
          transaction_hash_(std::move(transaction_hash)), receipt_(std::move(receipt))
    {
    }

    TransactionFailedError::TransactionFailedError(std::string transaction_id, std::string state,
                                                   const std::string &message)
        : std::runtime_error("relayer transaction " + transaction_id + " ended in " + state +
                             (message.empty() ? "" : ": " + message)),
          transaction_id_(std::move(transaction_id)), state_(std::move(state))
    {
    }

    TransactionTimeoutError::TransactionTimeoutError(std::string transaction_reference)
        : std::runtime_error("timed out waiting for transaction " + transaction_reference),
          transaction_reference_(std::move(transaction_reference))
    {
    }

    TransactionHandle::TransactionHandle(std::vector<std::string> transaction_hashes, std::string transaction_id,
                                         Waiter waiter)
        : transaction_hashes_(std::move(transaction_hashes)), transaction_id_(std::move(transaction_id)),
          waiter_(std::move(waiter))
    {
        if ((transaction_hashes_.empty() && transaction_id_.empty()) || !waiter_)
            throw std::invalid_argument("TransactionHandle needs a transaction hash or id, and a waiter");
    }

    const std::string &TransactionHandle::transaction_hash() const
    {
        static const std::string none;
        return transaction_hashes_.empty() ? none : transaction_hashes_.back();
    }

    TransactionOutcome TransactionHandle::wait(std::chrono::milliseconds timeout,
                                               std::chrono::milliseconds poll_interval) const
    {
        if (poll_interval <= std::chrono::milliseconds::zero())
            throw std::invalid_argument("poll_interval must be positive");
        return waiter_(timeout, poll_interval);
    }

    struct PositionClient::Impl
    {
        PolymarketContracts contracts;
        OrderSigner signer;
        std::string signer_address;
        std::string wallet; // EOA or Safe that holds positions
        SignatureType wallet_type;
        std::shared_ptr<EvmJsonRpcHttpClient> rpc;
        std::shared_ptr<detail::RelayerClient> relayer; // Safe only
        HttpClient gamma;
        std::chrono::milliseconds relayer_retry_delay;
        int relayer_max_submit_retries;
        std::mutex send_mutex; // one nonce sequence at a time
        bool chain_verified{false};

        explicit Impl(PositionClientConfig &config)
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

        // Caller holds send_mutex.
        void ensure_chain()
        {
            if (chain_verified)
                return;
            const auto node_chain = rpc->chain_id();
            if (node_chain != contracts.chain_id)
                throw std::runtime_error("RPC node is on chain " + std::to_string(node_chain) + ", expected " +
                                         std::to_string(contracts.chain_id));
            chain_verified = true;
        }

        // Caller holds send_mutex.
        std::string send_from_eoa(const ContractCall &call)
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

        TransactionHandle execute_from_eoa(const std::vector<ContractCall> &calls)
        {
            std::lock_guard<std::mutex> lock(send_mutex);
            std::vector<std::string> hashes;
            for (size_t i = 0; i < calls.size(); ++i)
            {
                hashes.push_back(send_from_eoa(calls[i]));
                // Later calls may depend on earlier ones, so each must land first.
                if (i + 1 < calls.size())
                    (void)wait_for_receipt(*rpc, hashes.back(), std::chrono::minutes(3), std::chrono::seconds(2));
            }
            auto rpc_client = rpc;
            auto last = hashes.back();
            return TransactionHandle(std::move(hashes), "",
                                     [rpc_client, last](std::chrono::milliseconds timeout, std::chrono::milliseconds poll)
                                     { return wait_for_receipt(*rpc_client, last, timeout, poll); });
        }

        TransactionHandle execute_from_safe(const std::vector<ContractCall> &calls, const std::string &metadata)
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
                                     { return wait_for_relayer(*relayer_client, *rpc_client, id, submit_hash, timeout, poll); });
        }
    };

    PositionClient::PositionClient(PositionClientConfig config)
    {
        config.contracts.validate();
        if (config.rpc_url.empty())
            throw std::invalid_argument("PositionClientConfig.rpc_url is required");
        impl_ = std::make_unique<Impl>(config);
    }

    PositionClient::~PositionClient() = default;

    const std::string &PositionClient::wallet_address() const
    {
        return impl_->wallet;
    }

    MarketPositionContext PositionClient::resolve_market(const std::string &condition_id, bool closed_only)
    {
        const auto normalized = detail::normalize_condition_id(condition_id);
        std::string path = "/markets?condition_ids=" + normalized;
        if (closed_only)
            path += "&closed=true";
        const auto response = impl_->gamma.get(path);
        if (!response.ok())
            throw std::runtime_error("Gamma market lookup failed for condition " + normalized + ": status " +
                                     std::to_string(response.status_code) + " " + response.error);
        nlohmann::json body;
        try
        {
            body = nlohmann::json::parse(response.body);
        }
        catch (const nlohmann::json::parse_error &)
        {
            throw std::runtime_error("Gamma market lookup returned invalid JSON for condition " + normalized);
        }
        return detail::market_context_from_gamma(body, normalized, impl_->contracts);
    }

    std::array<std::string, 2> PositionClient::position_balances(const MarketPositionContext &market)
    {
        const auto call = detail::balance_of_batch_call(market, wallet_address());
        return detail::decode_binary_balances(impl_->rpc->eth_call({call.to, call.data, "", ""}));
    }

    TransactionHandle PositionClient::execute_calls(const std::vector<ContractCall> &calls,
                                                    const std::string &metadata)
    {
        if (calls.empty())
            throw std::invalid_argument("execute_calls needs at least one call");
        if (metadata.size() > 500)
            throw std::invalid_argument("metadata must be at most 500 characters");
        if (impl_->wallet_type == SignatureType::POLY_GNOSIS_SAFE)
            return impl_->execute_from_safe(calls, metadata);
        return impl_->execute_from_eoa(calls);
    }

    TransactionHandle PositionClient::split_position(const std::string &condition_id, const std::string &amount)
    {
        detail::require_positive_amount(amount, "split amount");
        const auto market = resolve_market(condition_id);
        return execute_calls({detail::split_call(market, impl_->contracts, amount)},
                             "Split " + amount + " positions for condition " + market.condition_id);
    }

    TransactionHandle PositionClient::merge_positions(const std::string &condition_id, const std::string &amount)
    {
        const auto market = resolve_market(condition_id);
        const auto resolved = detail::resolve_merge_amount(market.condition_id, position_balances(market), amount);
        return execute_calls({detail::merge_call(market, impl_->contracts, resolved)},
                             "Merge " + resolved + " positions for condition " + market.condition_id);
    }

    TransactionHandle PositionClient::merge_multiple_positions(const std::vector<MergePositionRequest> &requests)
    {
        if (requests.empty())
            throw std::invalid_argument("merge_multiple_positions needs at least one request");
        std::set<std::string> seen;
        for (const auto &request : requests)
        {
            if (!seen.insert(detail::normalize_condition_id(request.condition_id)).second)
                throw std::invalid_argument("merge requests must reference distinct conditions");
        }
        std::vector<ContractCall> calls;
        for (const auto &request : requests)
        {
            const auto market = resolve_market(request.condition_id);
            const auto resolved =
                detail::resolve_merge_amount(market.condition_id, position_balances(market), request.amount);
            calls.push_back(detail::merge_call(market, impl_->contracts, resolved));
        }
        return execute_calls(calls, "Merge " + std::to_string(calls.size()) + " positions");
    }

    TransactionHandle PositionClient::redeem_positions(const std::string &condition_id)
    {
        const auto market = resolve_market(condition_id, true);
        const auto balances = position_balances(market);
        if (detail::uint256_is_zero(balances[0]) && detail::uint256_is_zero(balances[1]))
            throw std::invalid_argument("no position balance to redeem for condition " + market.condition_id);

        if (market.protocol == MarketProtocol::Ctf)
            return execute_calls({detail::ctf_redeem_positions_call(market.operator_contract,
                                                                    impl_->contracts.collateral_token,
                                                                    market.condition_id)},
                                 "Redeem positions for condition " + market.condition_id);

        std::vector<ContractCall> calls;
        for (unsigned outcome = 0; outcome < 2; ++outcome)
        {
            if (!detail::uint256_is_zero(balances[outcome]))
                calls.push_back(detail::router_redeem_call(market.operator_contract, market.condition_id, outcome,
                                                           balances[outcome]));
        }
        return execute_calls(calls, "Redeem positions for condition " + market.condition_id);
    }

} // namespace polymarket
