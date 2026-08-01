#pragma once

#include "evm_event_indexer.hpp"

namespace polymarket::detail
{
    inline EvmLogPosition evm_log_position(const EvmLog &log)
    {
        return {
            log.transaction_index.empty() ? 0 : evm_quantity_to_uint64(log.transaction_index),
            log.log_index.empty() ? 0 : evm_quantity_to_uint64(log.log_index),
            log.block_hash};
    }

    inline bool evm_position_at_or_before(const EvmLogPosition &left,
                                          const EvmLogPosition &right)
    {
        return left.transaction_index < right.transaction_index ||
               (left.transaction_index == right.transaction_index &&
                left.log_index <= right.log_index);
    }

    inline std::string evm_log_identity(const EvmLog &log)
    {
        if (log.block_hash.empty() || log.transaction_hash.empty() || log.log_index.empty())
            return "";
        return log.block_hash + ':' + log.transaction_hash + ':' + log.log_index;
    }

    inline uint64_t evm_log_block(const EvmLog &log)
    {
        return log.block_number.empty() ? 0 : evm_quantity_to_uint64(log.block_number);
    }

    inline bool evm_log_before(const EvmLog &left, const EvmLog &right)
    {
        const auto left_block = evm_log_block(left);
        const auto right_block = evm_log_block(right);
        if (left_block != right_block)
            return left_block < right_block;
        return !evm_position_at_or_before(evm_log_position(right),
                                          evm_log_position(left));
    }
}
