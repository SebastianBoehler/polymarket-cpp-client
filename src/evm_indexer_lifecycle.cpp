#include "evm_indexer_transport.hpp"
#include <stdexcept>
#include <thread>

namespace polymarket
{
    detail::EvmEventIndexerImpl::EvmEventIndexerImpl(
        EvmEventIndexerConfig config,
        std::shared_ptr<EvmBlockCursorStore> cursor_store,
        std::shared_ptr<EvmEventIndexerTransport> transport)
        : config_(std::move(config)),
          cursor_store_(std::move(cursor_store)),
          transport_(std::move(transport)),
          live_state_(std::make_unique<LiveState>())
    {
        if (!transport_)
            throw std::invalid_argument("EVM event indexer transport is required");
        if (config_.reorg_lookback_blocks == 0)
            throw std::invalid_argument("EVM reorg lookback must be positive");
    }

    detail::EvmEventIndexerImpl::~EvmEventIndexerImpl() = default;

    std::shared_ptr<detail::EvmEventIndexerImpl>
    detail::EvmEventIndexerImpl::create(
        EvmEventIndexerConfig config,
        std::shared_ptr<EvmBlockCursorStore> cursor_store,
        std::shared_ptr<EvmEventIndexerTransport> transport)
    {
        auto impl = std::shared_ptr<EvmEventIndexerImpl>(
            new EvmEventIndexerImpl(std::move(config), std::move(cursor_store),
                                    std::move(transport)));
        impl->start_workers();
        return impl;
    }

    void detail::EvmEventIndexerImpl::start_workers()
    {
        const auto self = shared_from_this();
        live_state_->delivery_thread = std::thread([self]()
                                                   {
                                                       {
                                                           std::lock_guard<std::mutex> lock(
                                                               self->live_state_->mutex);
                                                           self->live_state_->delivery_worker_id =
                                                               std::this_thread::get_id();
                                                       }
                                                       self->delivery_loop();
                                                   });
        try
        {
            live_state_->reconcile_thread = std::thread([self]()
                                                        {
                                                            {
                                                                std::lock_guard<std::mutex> lock(
                                                                    self->live_state_->mutex);
                                                                self->live_state_->reconcile_worker_id =
                                                                    std::this_thread::get_id();
                                                            }
                                                            self->reconciliation_loop();
                                                        });
        }
        catch (...)
        {
            shutdown_delivery_worker(false);
            throw;
        }
    }

    bool detail::EvmEventIndexerImpl::on_worker_thread() const
    {
        std::lock_guard<std::mutex> lock(live_state_->mutex);
        const auto current = std::this_thread::get_id();
        return current == live_state_->delivery_worker_id ||
               current == live_state_->reconcile_worker_id;
    }

    void detail::EvmEventIndexerImpl::shutdown()
    {
        const bool defer_join = on_worker_thread();
        stop();
        shutdown_reconciliation_worker(defer_join);
        shutdown_delivery_worker(defer_join);
    }

    EvmEventIndexer::EvmEventIndexer(
        EvmEventIndexerConfig config,
        std::shared_ptr<EvmBlockCursorStore> cursor_store)
        : EvmEventIndexer(config, std::move(cursor_store),
                          make_json_rpc_indexer_transport(config))
    {
    }

    EvmEventIndexer::EvmEventIndexer(
        EvmEventIndexerConfig config,
        std::shared_ptr<EvmBlockCursorStore> cursor_store,
        std::shared_ptr<EvmEventIndexerTransport> transport)
        : impl_(detail::EvmEventIndexerImpl::create(
              std::move(config), std::move(cursor_store), std::move(transport)))
    {
    }

    EvmEventIndexer::~EvmEventIndexer()
    {
        auto impl = std::move(impl_);
        if (impl)
            impl->shutdown();
    }

    void EvmEventIndexer::on_log(EvmIndexedLogCallback callback)
    {
        auto impl = impl_;
        impl->on_log(std::move(callback));
    }

    void EvmEventIndexer::on_checkpoint(EvmIndexCheckpointCallback callback)
    {
        auto impl = impl_;
        impl->on_checkpoint(std::move(callback));
    }

    void EvmEventIndexer::on_error(EvmRpcErrorCallback callback)
    {
        auto impl = impl_;
        impl->on_error(std::move(callback));
    }

    EvmCatchUpReport EvmEventIndexer::catch_up()
    {
        auto impl = impl_;
        return impl->catch_up();
    }

    bool EvmEventIndexer::start_live()
    {
        auto impl = impl_;
        return impl->start_live();
    }

    void EvmEventIndexer::run_live_for(std::chrono::seconds duration)
    {
        auto impl = impl_;
        impl->run_live_for(duration);
    }

    void EvmEventIndexer::stop()
    {
        auto impl = impl_;
        impl->stop();
    }
}
