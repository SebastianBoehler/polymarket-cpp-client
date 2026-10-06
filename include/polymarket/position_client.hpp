#pragma once

#include "polymarket/json_rpc_client.hpp"
#include "polymarket/order_signer.hpp"
#include "polymarket/polymarket_contracts.hpp"
#include "polymarket/trading_approvals.hpp"
#include <array>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
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

    // Thrown by TransactionHandle::wait when the relayer reports a terminal
    // failure (STATE_FAILED or STATE_INVALID) for a gasless transaction.
    class TransactionFailedError : public std::runtime_error
    {
    public:
        TransactionFailedError(std::string transaction_id, std::string state, const std::string &message);
        const std::string &transaction_id() const { return transaction_id_; }
        const std::string &state() const { return state_; }

    private:
        std::string transaction_id_;
        std::string state_;
    };

    // Thrown by TransactionHandle::wait when no outcome arrives before the timeout.
    // The transaction may still be mined later.
    class TransactionTimeoutError : public std::runtime_error
    {
    public:
        explicit TransactionTimeoutError(std::string transaction_reference);
        // Transaction hash, or the relayer transaction id when no hash is known yet.
        const std::string &transaction_reference() const { return transaction_reference_; }

    private:
        std::string transaction_reference_;
    };

    // Thrown when an EOA batch fails after at least one of its transactions
    // reached the node. Those transactions are not rolled back.
    class PartialBatchError : public std::runtime_error
    {
    public:
        PartialBatchError(std::vector<std::string> submitted_hashes, size_t failed_call_index, size_t call_count,
                          std::exception_ptr cause, const std::string &cause_message);
        // One hash per call that reached the node, in call order. When it has
        // more than failed_call_index() entries, the failing call was sent and
        // then reverted or timed out while waiting to be mined.
        const std::vector<std::string> &submitted_hashes() const { return submitted_hashes_; }
        size_t failed_call_index() const { return failed_call_index_; }
        // The original error; std::rethrow_exception(cause()) to inspect its type.
        std::exception_ptr cause() const { return cause_; }

    private:
        std::vector<std::string> submitted_hashes_;
        size_t failed_call_index_;
        std::exception_ptr cause_;
    };

    struct TransactionOutcome
    {
        std::string transaction_hash;
        std::string transaction_id; // relayer id; empty for EOA transactions
        EvmTransactionReceipt receipt;
    };

    // A submitted transaction. Submission returns as soon as the node or
    // relayer accepts it; wait() blocks until it is mined.
    class TransactionHandle
    {
    public:
        using Waiter = std::function<TransactionOutcome(std::chrono::milliseconds timeout,
                                                        std::chrono::milliseconds poll_interval)>;

        TransactionHandle(std::vector<std::string> transaction_hashes, std::string transaction_id, Waiter waiter);

        // Hash of the final transaction, or empty while a relayer transaction
        // has no hash yet (wait() returns it). An EOA batch sends one
        // transaction per call; the earlier ones are already mined.
        const std::string &transaction_hash() const;
        const std::vector<std::string> &transaction_hashes() const { return transaction_hashes_; }
        // Relayer transaction id; empty for EOA transactions.
        const std::string &transaction_id() const { return transaction_id_; }

        // Returns the receipt on success. Throws TransactionRevertedError,
        // TransactionFailedError or TransactionTimeoutError; transport failures
        // propagate as std::runtime_error.
        TransactionOutcome wait(std::chrono::milliseconds timeout = std::chrono::minutes(3),
                                std::chrono::milliseconds poll_interval = std::chrono::seconds(2)) const;

    private:
        std::vector<std::string> transaction_hashes_;
        std::string transaction_id_;
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
        std::string rpc_url; // balance reads, receipts, and EOA transactions
        std::string gamma_api_url = "https://gamma-api.polymarket.com";
        PolymarketContracts contracts = PolymarketContracts::polygon_mainnet();
        long rpc_timeout_ms = 15000;

        // EOA sends directly and pays gas. POLY_GNOSIS_SAFE submits gasless
        // Safe transactions through the relayer; other types are not supported yet.
        SignatureType wallet_type{SignatureType::EOA};
        // The Safe for POLY_GNOSIS_SAFE. Derived from the key when empty; a
        // value that differs from the derived Safe is rejected.
        std::string funder_address;

        std::string relayer_url = "https://relayer-v2.polymarket.com";
        std::string relayer_api_key;
        std::string relayer_api_key_address; // defaults to the signer address
        long relayer_retry_delay_ms = 2000;  // between retried submits
        int relayer_max_submit_retries = 10;
    };

    // Split, merge and redeem binary market positions (CTF and Protocol V2
    // markets) held by an EOA or a Polymarket Gnosis Safe. Amounts are integer
    // base units (pUSD and outcome tokens use 6 decimals) as base-10 strings.
    // condition_id is 0x + 32 bytes of hex, or 31 bytes for a Protocol V2 market.
    //
    // The operations do not check approvals: split needs a pUSD allowance for
    // the operator contract, and merge/redeem need ERC-1155 approval for it.
    // setup_trading_approvals() grants all of them once per wallet.
    class PositionClient
    {
    public:
        explicit PositionClient(PositionClientConfig config);
        ~PositionClient();

        PositionClient(const PositionClient &) = delete;
        PositionClient &operator=(const PositionClient &) = delete;

        // Address that holds positions: the EOA, or the Safe.
        const std::string &wallet_address() const;

        // Lock `amount` pUSD into one YES and one NO share per unit.
        TransactionHandle split_position(const std::string &condition_id, const std::string &amount);

        // Turn `amount` YES+NO pairs back into pUSD; "max" merges min(YES, NO).
        TransactionHandle merge_positions(const std::string &condition_id,
                                          const std::string &amount = "max");

        // Merges several conditions. A Safe batches them into one atomic
        // MultiSend transaction. An EOA is not atomic: one transaction per
        // condition, each mined before the next is sent, and the handle tracks
        // the last one; if a merge fails after an earlier one was sent,
        // PartialBatchError is thrown and the remaining merges are not sent.
        TransactionHandle merge_multiple_positions(const std::vector<MergePositionRequest> &requests);

        // Redeem every resolved position the wallet holds in a closed market.
        TransactionHandle redeem_positions(const std::string &condition_id);

        // Market lookup used by the operations above (Gamma /markets).
        MarketPositionContext resolve_market(const std::string &condition_id, bool closed_only = false);

        // On-chain [yes, no] balances of the wallet, in base units (decimal).
        std::array<std::string, 2> position_balances(const MarketPositionContext &market);

        // Which of required_trading_approvals() the wallet (default: this
        // client's wallet) still lacks, read with one eth_call per approval.
        TradingApprovalsState
        get_trading_approvals_state(const std::optional<std::string> &wallet = std::nullopt);

        // Grants the missing trading approvals and waits for them to be mined.
        // A Safe sends them as one atomic MultiSend; an EOA sends one
        // transaction per approval (see merge_multiple_positions). Returns
        // nullopt without sending anything when the wallet is fully approved.
        std::optional<TransactionOutcome>
        setup_trading_approvals(std::chrono::milliseconds timeout = std::chrono::minutes(3));

        // ERC-20 approve(spender, amount). amount is a uint256 in base units,
        // or "max"; "0" revokes.
        TransactionHandle approve_erc20(const std::string &token_address,
                                        const std::string &spender_address,
                                        const std::string &amount,
                                        const std::string &metadata = "");

        // ERC-1155 setApprovalForAll(operator, approved).
        TransactionHandle approve_erc1155_for_all(const std::string &token_address,
                                                  const std::string &operator_address,
                                                  bool approved = true,
                                                  const std::string &metadata = "");

        // ERC-20 transfer(recipient, amount) from the wallet; amount is a
        // positive uint256 in base units.
        TransactionHandle transfer_erc20(const std::string &token_address,
                                         const std::string &recipient_address,
                                         const std::string &amount,
                                         const std::string &metadata = "");

        // Low-level: send arbitrary calls from the wallet. metadata (at most
        // 500 characters) is attached to relayer submissions.
        TransactionHandle execute_calls(const std::vector<ContractCall> &calls, const std::string &metadata = "");

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace polymarket
