#pragma once

#include "json_rpc_client.hpp"
#include "polymarket_contracts.hpp"
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace polymarket
{
    // One contract call inside a transaction.
    struct ContractCall
    {
        std::string to;
        std::string data;        // 0x-hex calldata
        std::string value = "0"; // wei
    };

    // Thrown by TransactionHandle::wait when the transaction was mined but reverted.
    class TransactionRevertedError : public std::runtime_error
    {
    public:
        TransactionRevertedError(std::string transaction_hash, EvmTransactionReceipt receipt);
        const std::string &transaction_hash() const { return transaction_hash_; }
        const EvmTransactionReceipt &receipt() const { return receipt_; }

    private:
        std::string transaction_hash_;
        EvmTransactionReceipt receipt_;
    };

    // Thrown by TransactionHandle::wait when no outcome arrives before the timeout.
    // The transaction may still be mined later.
    class TransactionTimeoutError : public std::runtime_error
    {
    public:
        explicit TransactionTimeoutError(std::string transaction_hash);
        const std::string &transaction_hash() const { return transaction_hash_; }

    private:
        std::string transaction_hash_;
    };

    struct TransactionOutcome
    {
        std::string transaction_hash;
        EvmTransactionReceipt receipt;
    };

    // A submitted transaction. Submission returns as soon as the node accepts
    // it; wait() blocks until it is mined.
    class TransactionHandle
    {
    public:
        using Waiter = std::function<TransactionOutcome(std::chrono::milliseconds timeout,
                                                        std::chrono::milliseconds poll_interval)>;

        TransactionHandle(std::vector<std::string> transaction_hashes, Waiter waiter);

        // Hash of the final transaction. An EOA batch sends one transaction per
        // call; the earlier ones are already mined when the handle is returned.
        const std::string &transaction_hash() const { return transaction_hashes_.back(); }
        const std::vector<std::string> &transaction_hashes() const { return transaction_hashes_; }

        // Returns the receipt on success. Throws TransactionRevertedError or
        // TransactionTimeoutError; RPC failures propagate as std::runtime_error.
        TransactionOutcome wait(std::chrono::milliseconds timeout = std::chrono::minutes(3),
                                std::chrono::milliseconds poll_interval = std::chrono::seconds(2)) const;

    private:
        std::vector<std::string> transaction_hashes_;
        Waiter waiter_;
    };

    enum class MarketProtocol
    {
        Ctf, // V1: ConditionalTokens via the pUSD collateral adapters
        V2   // Protocol V2 router
    };

    // How a market's binary positions are held and which contract operates on them.
    struct MarketPositionContext
    {
        MarketProtocol protocol{MarketProtocol::Ctf};
        std::string market_id;
        std::string condition_id;
        bool neg_risk{false};
        std::array<std::string, 2> token_ids;  // [yes, no] ERC-1155 ids (decimal)
        std::string position_token_contract;   // ERC-1155 contract holding token_ids
        std::string operator_contract;         // collateral adapter (Ctf) or V2 router
    };

    // A merge in merge_multiple_positions. amount is base units (6 decimals)
    // or "max" for the largest balanced amount the wallet holds.
    struct MergePositionRequest
    {
        std::string condition_id;
        std::string amount = "max";
    };

    struct PositionClientConfig
    {
        std::string private_key;
        std::string rpc_url;
        std::string gamma_api_url = "https://gamma-api.polymarket.com";
        PolymarketContracts contracts = PolymarketContracts::polygon_mainnet();
        long rpc_timeout_ms = 15000;
    };

    // Split, merge and redeem binary market positions (CTF and Protocol V2
    // markets). This client sends transactions directly from the EOA of
    // private_key, which pays gas in POL. Amounts are integer base units
    // (pUSD and outcome tokens use 6 decimals) as base-10 strings.
    //
    // Approvals are not checked: split needs a pUSD allowance for the operator
    // contract, and merge/redeem need ERC-1155 approval for it.
    class PositionClient
    {
    public:
        explicit PositionClient(PositionClientConfig config);
        ~PositionClient();

        PositionClient(const PositionClient &) = delete;
        PositionClient &operator=(const PositionClient &) = delete;

        // Address that holds positions and sends transactions.
        const std::string &wallet_address() const;

        // Lock `amount` pUSD into one YES and one NO share per unit.
        TransactionHandle split_position(const std::string &condition_id, const std::string &amount);

        // Turn `amount` YES+NO pairs back into pUSD; "max" merges min(YES, NO).
        TransactionHandle merge_positions(const std::string &condition_id,
                                          const std::string &amount = "max");

        // Merges several conditions. Not atomic: one transaction per condition,
        // each mined before the next is sent; the handle tracks the last one.
        // If an earlier transaction reverts, TransactionRevertedError is thrown
        // and the remaining merges are not sent.
        TransactionHandle merge_multiple_positions(const std::vector<MergePositionRequest> &requests);

        // Redeem every resolved position the wallet holds in a closed market.
        TransactionHandle redeem_positions(const std::string &condition_id);

        // Market lookup used by the operations above (Gamma /markets).
        MarketPositionContext resolve_market(const std::string &condition_id, bool closed_only = false);

        // On-chain [yes, no] balances of the wallet, in base units (decimal).
        std::array<std::string, 2> position_balances(const MarketPositionContext &market);

        // Low-level: send arbitrary calls from the wallet.
        TransactionHandle execute_calls(const std::vector<ContractCall> &calls);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace polymarket
