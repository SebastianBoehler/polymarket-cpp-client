#include "position_client.hpp"
#include "evm_transaction.hpp"
#include "evm_uint.hpp"
#include "evm_utils.hpp"
#include "http_client.hpp"
#include "order_signer.hpp"
#include "position_calls.hpp"
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
                    return {transaction_hash, std::move(*receipt)};
                }
                if (std::chrono::steady_clock::now() + poll_interval > deadline)
                    throw TransactionTimeoutError(transaction_hash);
                std::this_thread::sleep_for(poll_interval);
            }
        }
    } // namespace

    TransactionRevertedError::TransactionRevertedError(std::string transaction_hash, EvmTransactionReceipt receipt)
        : std::runtime_error("transaction " + transaction_hash + " reverted"),
          transaction_hash_(std::move(transaction_hash)), receipt_(std::move(receipt))
    {
    }

    TransactionTimeoutError::TransactionTimeoutError(std::string transaction_hash)
        : std::runtime_error("timed out waiting for transaction " + transaction_hash),
          transaction_hash_(std::move(transaction_hash))
    {
    }

    TransactionHandle::TransactionHandle(std::vector<std::string> transaction_hashes, Waiter waiter)
        : transaction_hashes_(std::move(transaction_hashes)), waiter_(std::move(waiter))
    {
        if (transaction_hashes_.empty() || !waiter_)
            throw std::invalid_argument("TransactionHandle needs a transaction hash and a waiter");
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
        std::string address;
        std::shared_ptr<EvmJsonRpcHttpClient> rpc;
        HttpClient gamma;
        std::mutex send_mutex; // one nonce sequence at a time
        bool chain_verified{false};

        explicit Impl(PositionClientConfig &config)
            : contracts(std::move(config.contracts)),
              signer(config.private_key, static_cast<int>(contracts.chain_id)),
              address(signer.address()),
              rpc(std::make_shared<EvmJsonRpcHttpClient>(config.rpc_url))
        {
            rpc->set_timeout_ms(config.rpc_timeout_ms);
            gamma.set_base_url(config.gamma_api_url);
        }

        // Caller holds send_mutex.
        std::string send(const ContractCall &call)
        {
            if (!chain_verified)
            {
                const auto node_chain = rpc->chain_id();
                if (node_chain != contracts.chain_id)
                    throw std::runtime_error("RPC node is on chain " + std::to_string(node_chain) +
                                             ", expected " + std::to_string(contracts.chain_id));
                chain_verified = true;
            }
            const auto &from = address;
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
        return impl_->address;
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

    TransactionHandle PositionClient::execute_calls(const std::vector<ContractCall> &calls)
    {
        if (calls.empty())
            throw std::invalid_argument("execute_calls needs at least one call");
        std::lock_guard<std::mutex> lock(impl_->send_mutex);
        std::vector<std::string> hashes;
        for (size_t i = 0; i < calls.size(); ++i)
        {
            hashes.push_back(impl_->send(calls[i]));
            // Later calls may depend on earlier ones, so each must land first.
            if (i + 1 < calls.size())
                (void)wait_for_receipt(*impl_->rpc, hashes.back(), std::chrono::minutes(3), std::chrono::seconds(2));
        }
        auto rpc = impl_->rpc;
        auto last = hashes.back();
        return TransactionHandle(std::move(hashes),
                                 [rpc, last](std::chrono::milliseconds timeout, std::chrono::milliseconds poll)
                                 { return wait_for_receipt(*rpc, last, timeout, poll); });
    }

    TransactionHandle PositionClient::split_position(const std::string &condition_id, const std::string &amount)
    {
        detail::require_positive_amount(amount, "split amount");
        const auto market = resolve_market(condition_id);
        return execute_calls({detail::split_call(market, impl_->contracts, amount)});
    }

    TransactionHandle PositionClient::merge_positions(const std::string &condition_id, const std::string &amount)
    {
        const auto market = resolve_market(condition_id);
        const auto resolved = detail::resolve_merge_amount(market.condition_id, position_balances(market), amount);
        return execute_calls({detail::merge_call(market, impl_->contracts, resolved)});
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
        return execute_calls(calls);
    }

    TransactionHandle PositionClient::redeem_positions(const std::string &condition_id)
    {
        const auto market = resolve_market(condition_id, true);
        const auto balances = position_balances(market);
        if (detail::uint256_is_zero(balances[0]) && detail::uint256_is_zero(balances[1]))
            throw std::invalid_argument("no position balance to redeem for condition " + market.condition_id);

        if (market.protocol == MarketProtocol::Ctf)
            return execute_calls({detail::ctf_redeem_positions_call(
                market.operator_contract, impl_->contracts.collateral_token, market.condition_id)});

        std::vector<ContractCall> calls;
        for (unsigned outcome = 0; outcome < 2; ++outcome)
        {
            if (!detail::uint256_is_zero(balances[outcome]))
                calls.push_back(detail::router_redeem_call(market.operator_contract, market.condition_id, outcome,
                                                           balances[outcome]));
        }
        return execute_calls(calls);
    }

} // namespace polymarket
