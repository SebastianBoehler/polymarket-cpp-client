#include "transaction_waiters.hpp"
#include <algorithm>
#include <thread>

namespace polymarket
{
    namespace
    {
        std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline)
        {
            return std::max(std::chrono::milliseconds::zero(),
                            std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
        }

        // Sleeps until the next poll, never past the deadline, so a timeout
        // shorter than the poll interval still gets a final poll at the
        // deadline. Returns false once the deadline has passed.
        bool sleep_before_next_poll(std::chrono::steady_clock::time_point deadline,
                                    std::chrono::milliseconds poll_interval)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::min(poll_interval, remaining(deadline)));
            return true;
        }
    } // namespace

    namespace detail
    {
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
                if (!sleep_before_next_poll(deadline, poll_interval))
                    throw TransactionTimeoutError(transaction_hash);
            }
        }

        TransactionOutcome wait_for_relayer(RelayerClient &relayer, EvmJsonRpcHttpClient &rpc,
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
                if (!sleep_before_next_poll(deadline, poll_interval))
                    throw TransactionTimeoutError(tx.transaction_hash.empty() ? transaction_id : tx.transaction_hash);
            }
        }
    } // namespace detail

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

    namespace
    {
        std::string partial_batch_message(const std::vector<std::string> &hashes, size_t failed_call_index,
                                          size_t call_count, const std::string &cause_message)
        {
            std::string message = "EOA batch failed at call " + std::to_string(failed_call_index) + " of " +
                                  std::to_string(call_count) + " after submitting";
            for (size_t i = 0; i < hashes.size(); ++i)
                message += (i == 0 ? " " : ", ") + hashes[i];
            return message + ": " + cause_message;
        }
    } // namespace

    PartialBatchError::PartialBatchError(std::vector<std::string> submitted_hashes, size_t failed_call_index,
                                         size_t call_count, std::exception_ptr cause,
                                         const std::string &cause_message)
        : std::runtime_error(partial_batch_message(submitted_hashes, failed_call_index, call_count, cause_message)),
          submitted_hashes_(std::move(submitted_hashes)), failed_call_index_(failed_call_index),
          cause_(std::move(cause))
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

} // namespace polymarket
