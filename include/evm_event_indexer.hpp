#pragma once

#include "evm_utils.hpp"
#include "json_rpc_client.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{
    namespace detail
    {
        class EvmEventIndexerImpl;
    }

    struct EvmBlockRange
    {
        uint64_t from_block{0};
        uint64_t to_block{0};
    };

    uint64_t evm_quantity_to_uint64(const std::string &quantity);
    std::string evm_uint64_to_quantity(uint64_t value);
    std::vector<EvmBlockRange> evm_make_block_ranges(uint64_t from_block,
                                                     uint64_t to_block,
                                                     uint64_t batch_size);

    struct EvmLogPosition
    {
        uint64_t transaction_index{0};
        uint64_t log_index{0};
        std::string block_hash;
    };

    struct EvmIndexCursor
    {
        uint64_t block_number{0};
        bool block_complete{true};
        std::optional<EvmLogPosition> last_log;
    };

    class EvmBlockCursorStore
    {
    public:
        virtual ~EvmBlockCursorStore() = default;
        virtual std::optional<uint64_t> load(const std::string &cursor_name) = 0;
        virtual void save(const std::string &cursor_name, uint64_t block_number) = 0;

        virtual std::optional<EvmIndexCursor> load_cursor(const std::string &cursor_name);
        virtual void save_cursor(const std::string &cursor_name, const EvmIndexCursor &cursor);
        virtual void rewind(const std::string &cursor_name, uint64_t from_block);
    };

    class FileBlockCursorStore : public EvmBlockCursorStore
    {
    public:
        explicit FileBlockCursorStore(std::string path);

        std::optional<uint64_t> load(const std::string &cursor_name) override;
        void save(const std::string &cursor_name, uint64_t block_number) override;
        std::optional<EvmIndexCursor> load_cursor(const std::string &cursor_name) override;
        void save_cursor(const std::string &cursor_name, const EvmIndexCursor &cursor) override;
        void rewind(const std::string &cursor_name, uint64_t from_block) override;

    private:
        std::string path_;
        std::shared_ptr<std::mutex> mutex_;
    };

    struct EvmEventIndexerConfig
    {
        std::string rpc_http_url;
        std::string rpc_ws_url;
        EvmLogFilter filter;
        std::string cursor_name{"default"};
        uint64_t start_block{0};
        uint64_t confirmations{0};
        uint64_t batch_size{2000};
        uint64_t reorg_lookback_blocks{64};
        int ws_ping_interval_ms{5000};
    };

    struct EvmIndexedLog
    {
        EvmLog log;
        bool live{false};
        uint64_t received_at_ms{0};
    };

    struct EvmCatchUpReport
    {
        uint64_t from_block{0};
        uint64_t to_block{0};
        uint64_t ranges_scanned{0};
        uint64_t logs_seen{0};
    };

    using EvmIndexedLogCallback = std::function<void(const EvmIndexedLog &)>;
    using EvmIndexCheckpointCallback = std::function<void(uint64_t block_number)>;
    using EvmHeadCallback = std::function<void(uint64_t block_number)>;

    // Network boundary used by the indexer. start_logs returns only after the
    // subscription is active, allowing callers to capture a race-free boundary.
    class EvmEventIndexerTransport
    {
    public:
        virtual ~EvmEventIndexerTransport() = default;
        virtual std::string block_number() = 0;
        virtual std::vector<EvmLog> get_logs(const EvmLogFilter &filter) = 0;
        virtual bool start_logs(const EvmLogFilter &filter,
                                EvmLogCallback log_callback,
                                EvmHeadCallback head_callback,
                                EvmRpcErrorCallback error_callback) = 0;
        virtual void stop() = 0;
    };

    class EvmEventIndexer
    {
    public:
        EvmEventIndexer(EvmEventIndexerConfig config,
                        std::shared_ptr<EvmBlockCursorStore> cursor_store);
        EvmEventIndexer(EvmEventIndexerConfig config,
                        std::shared_ptr<EvmBlockCursorStore> cursor_store,
                        std::shared_ptr<EvmEventIndexerTransport> transport);
        ~EvmEventIndexer();

        EvmEventIndexer(const EvmEventIndexer &) = delete;
        EvmEventIndexer &operator=(const EvmEventIndexer &) = delete;

        void on_log(EvmIndexedLogCallback callback);
        void on_checkpoint(EvmIndexCheckpointCallback callback);
        void on_error(EvmRpcErrorCallback callback);

        EvmCatchUpReport catch_up();
        bool start_live();
        void run_live_for(std::chrono::seconds duration);
        void stop();

    private:
        std::shared_ptr<detail::EvmEventIndexerImpl> impl_;
    };

} // namespace polymarket
