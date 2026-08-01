#include "evm_indexer_transport.hpp"

namespace polymarket
{
    void detail::EvmEventIndexerImpl::on_log(EvmIndexedLogCallback callback)
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        log_cb_.swap(callback);
    }

    void detail::EvmEventIndexerImpl::on_checkpoint(
        EvmIndexCheckpointCallback callback)
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        checkpoint_cb_.swap(callback);
    }

    void detail::EvmEventIndexerImpl::on_error(EvmRpcErrorCallback callback)
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        error_cb_.swap(callback);
    }

    EvmIndexedLogCallback detail::EvmEventIndexerImpl::log_callback() const
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        return log_cb_;
    }

    EvmIndexCheckpointCallback
    detail::EvmEventIndexerImpl::checkpoint_callback() const
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        return checkpoint_cb_;
    }

    EvmRpcErrorCallback detail::EvmEventIndexerImpl::error_callback() const
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        return error_cb_;
    }

    bool detail::EvmEventIndexerImpl::delivery_shutdown_requested() const
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        return live_state_->delivery_shutdown;
    }

    void detail::EvmEventIndexerImpl::emit_error(std::string_view message) noexcept
    {
        try
        {
            if (auto callback = error_callback())
            {
                const std::string owned_message(message);
                callback(owned_message);
            }
        }
        catch (...)
        {
        }
    }
}
