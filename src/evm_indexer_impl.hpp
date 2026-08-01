#pragma once

#include "evm_event_indexer.hpp"
#include <string_view>

namespace polymarket::detail
{
    class EvmEventIndexerImpl final
        : public std::enable_shared_from_this<EvmEventIndexerImpl>
    {
    public:
        static std::shared_ptr<EvmEventIndexerImpl>
        create(EvmEventIndexerConfig config,
               std::shared_ptr<EvmBlockCursorStore> cursor_store,
               std::shared_ptr<EvmEventIndexerTransport> transport);
        ~EvmEventIndexerImpl();

        void shutdown();
        void on_log(EvmIndexedLogCallback callback);
        void on_checkpoint(EvmIndexCheckpointCallback callback);
        void on_error(EvmRpcErrorCallback callback);
        EvmCatchUpReport catch_up();
        bool start_live();
        void run_live_for(std::chrono::seconds duration);
        void stop();

        struct LiveState;

    private:
        EvmEventIndexerImpl(EvmEventIndexerConfig config,
                            std::shared_ptr<EvmBlockCursorStore> cursor_store,
                            std::shared_ptr<EvmEventIndexerTransport> transport);
        void start_workers();
        bool on_worker_thread() const;

        EvmCatchUpReport catch_up_to(uint64_t target, uint64_t durable_through,
                                    uint64_t generation);
        bool scan_ranges(uint64_t from, uint64_t to, bool persist,
                         const std::optional<EvmIndexCursor> &resume,
                         EvmCatchUpReport &report, uint64_t generation);
        bool emit_log(const EvmLog &log, bool live);
        bool dispatch_log(const EvmLog &log, bool live, uint64_t generation, bool wait);
        void delivery_loop();
        void shutdown_delivery_worker(bool defer_join);
        bool generation_current(uint64_t generation) const;
        void prune_delivered(uint64_t durable_block);
        void drain_live_buffer(uint64_t generation);
        void process_live_log(const EvmLog &log, uint64_t generation, bool wait);
        void handle_live_head(uint64_t block_number, uint64_t generation);
        void reconciliation_loop();
        void reconcile_confirmed(uint64_t head, uint64_t generation,
                                 bool include_unconfirmed, bool recover_overlap);
        void reconcile_gap(uint64_t target, uint64_t durable_through,
                           uint64_t generation);
        bool scan_gap_ranges(uint64_t from, uint64_t to, bool persist,
                             uint64_t generation);
        void schedule_overlap_recovery(uint64_t generation);
        void report_reconciliation_failure(uint64_t generation,
                                           bool recover_overlap,
                                           std::string_view message) noexcept;
        void shutdown_reconciliation_worker(bool defer_join);
        void save_checkpoint(uint64_t block_number);
        void save_log_checkpoint(const EvmLog &log, bool notify);
        void emit_error(std::string_view message) noexcept;
        EvmIndexedLogCallback log_callback() const;
        EvmIndexCheckpointCallback checkpoint_callback() const;
        EvmRpcErrorCallback error_callback() const;
        bool delivery_shutdown_requested() const;
        void handle_live_log(const EvmLog &log, uint64_t generation);
        void handle_live_error(const std::string &error, uint64_t generation);

        EvmEventIndexerConfig config_;
        std::shared_ptr<EvmBlockCursorStore> cursor_store_;
        std::shared_ptr<EvmEventIndexerTransport> transport_;
        std::unique_ptr<LiveState> live_state_;
        EvmIndexedLogCallback log_cb_;
        EvmIndexCheckpointCallback checkpoint_cb_;
        EvmRpcErrorCallback error_cb_;
    };
}
