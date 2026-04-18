#pragma once

#include "evm_utils.hpp"
#include "json_rpc_client.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{

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

    class EvmBlockCursorStore
    {
    public:
        virtual ~EvmBlockCursorStore() = default;
        virtual std::optional<uint64_t> load(const std::string &cursor_name) = 0;
        virtual void save(const std::string &cursor_name, uint64_t block_number) = 0;
    };

    class FileBlockCursorStore : public EvmBlockCursorStore
    {
    public:
        explicit FileBlockCursorStore(std::string path);

        std::optional<uint64_t> load(const std::string &cursor_name) override;
        void save(const std::string &cursor_name, uint64_t block_number) override;

    private:
        std::string path_;
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

    class EvmEventIndexer
    {
    public:
        EvmEventIndexer(EvmEventIndexerConfig config,
                        std::shared_ptr<EvmBlockCursorStore> cursor_store);
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
        EvmEventIndexerConfig config_;
        std::shared_ptr<EvmBlockCursorStore> cursor_store_;
        EvmJsonRpcHttpClient http_;
        EvmJsonRpcWsClient ws_;

        EvmIndexedLogCallback log_cb_;
        EvmIndexCheckpointCallback checkpoint_cb_;
        EvmRpcErrorCallback error_cb_;

        void save_checkpoint(uint64_t block_number);
        void emit_error(const std::string &message);
        void handle_live_log(const EvmLog &log);
    };

} // namespace polymarket
