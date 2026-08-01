#pragma once

#include "evm_event_indexer.hpp"
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

inline polymarket::EvmLog make_indexer_log(uint64_t block, uint64_t transaction_index,
                                           uint64_t log_index, std::string transaction_hash)
{
    polymarket::EvmLog log;
    log.address = "0xfeed";
    log.block_hash = "0xblock" + std::to_string(block);
    log.block_number = polymarket::evm_uint64_to_quantity(block);
    log.transaction_hash = std::move(transaction_hash);
    log.transaction_index = polymarket::evm_uint64_to_quantity(transaction_index);
    log.log_index = polymarket::evm_uint64_to_quantity(log_index);
    log.data = "0x";
    return log;
}

class FakeEvmEventTransport final : public polymarket::EvmEventIndexerTransport
{
public:
    std::string head{"0x0"};
    std::vector<polymarket::EvmLog> logs;
    std::function<void(const polymarket::EvmLogFilter &)> during_get_logs;
    bool subscribed{false};
    bool start_result{true};
    bool require_subscription_before_head{false};
    uint32_t stop_calls{0};

    std::string block_number() override
    {
        if (require_subscription_before_head && !subscribed)
            throw std::runtime_error("head captured before live subscription");
        return head;
    }

    std::vector<polymarket::EvmLog> get_logs(const polymarket::EvmLogFilter &filter) override
    {
        if (during_get_logs)
            during_get_logs(filter);
        const auto from = polymarket::evm_quantity_to_uint64(filter.from_block);
        const auto to = polymarket::evm_quantity_to_uint64(filter.to_block);
        std::vector<polymarket::EvmLog> selected;
        for (const auto &log : logs)
        {
            const auto block = polymarket::evm_quantity_to_uint64(log.block_number);
            if (block >= from && block <= to)
                selected.push_back(log);
        }
        return selected;
    }

    bool start_logs(const polymarket::EvmLogFilter &,
                    polymarket::EvmLogCallback log_callback,
                    polymarket::EvmHeadCallback head_callback,
                    polymarket::EvmRpcErrorCallback error_callback) override
    {
        log_callbacks.push_back(std::move(log_callback));
        head_callbacks.push_back(std::move(head_callback));
        error_callbacks.push_back(std::move(error_callback));
        subscribed = true;
        return start_result;
    }

    void stop() override
    {
        ++stop_calls;
        subscribed = false;
    }

    void emit(const polymarket::EvmLog &log) { emit_from(log_callbacks.size() - 1, log); }

    void emit_from(size_t subscription, const polymarket::EvmLog &log)
    {
        if (log_callbacks.at(subscription))
            log_callbacks.at(subscription)(log);
    }

    void emit_head(uint64_t block)
    {
        if (!head_callbacks.empty() && head_callbacks.back())
            head_callbacks.back()(block);
    }

    void emit_error(const std::string &error)
    {
        if (!error_callbacks.empty() && error_callbacks.back())
            error_callbacks.back()(error);
    }

    std::vector<polymarket::EvmLogCallback> log_callbacks;
    std::vector<polymarket::EvmHeadCallback> head_callbacks;
    std::vector<polymarket::EvmRpcErrorCallback> error_callbacks;
};
