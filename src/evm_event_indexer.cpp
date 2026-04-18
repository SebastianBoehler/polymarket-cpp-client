#include "evm_event_indexer.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace polymarket
{
    namespace
    {
        std::string strip_0x(std::string value)
        {
            if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
                return value.substr(2);
            return value;
        }

        nlohmann::json read_cursor_file(const std::string &path)
        {
            if (!std::filesystem::exists(path))
                return nlohmann::json::object();
            std::ifstream input(path);
            if (!input)
                throw std::runtime_error("failed to open cursor file: " + path);
            if (input.peek() == std::ifstream::traits_type::eof())
                return nlohmann::json::object();
            return nlohmann::json::parse(input);
        }

        uint64_t received_at_ms()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }
    } // namespace

    uint64_t evm_quantity_to_uint64(const std::string &quantity)
    {
        if (quantity.empty())
            throw std::invalid_argument("empty EVM quantity");
        auto clean = strip_0x(quantity);
        return std::stoull(clean, nullptr, quantity == clean ? 10 : 16);
    }

    std::string evm_uint64_to_quantity(uint64_t value)
    {
        std::ostringstream out;
        out << "0x" << std::hex << value;
        return out.str();
    }

    std::vector<EvmBlockRange> evm_make_block_ranges(uint64_t from_block,
                                                     uint64_t to_block,
                                                     uint64_t batch_size)
    {
        if (batch_size == 0)
            throw std::invalid_argument("batch_size must be positive");
        std::vector<EvmBlockRange> ranges;
        if (from_block > to_block)
            return ranges;
        for (uint64_t from = from_block; from <= to_block;)
        {
            uint64_t remaining = to_block - from;
            uint64_t to = from + std::min(batch_size - 1, remaining);
            ranges.push_back({from, to});
            if (to == to_block)
                break;
            from = to + 1;
        }
        return ranges;
    }

    FileBlockCursorStore::FileBlockCursorStore(std::string path)
        : path_(std::move(path))
    {
    }

    std::optional<uint64_t> FileBlockCursorStore::load(const std::string &cursor_name)
    {
        auto data = read_cursor_file(path_);
        if (!data.contains(cursor_name))
            return std::nullopt;
        if (data[cursor_name].is_number_unsigned())
            return data[cursor_name].get<uint64_t>();
        return evm_quantity_to_uint64(data[cursor_name].get<std::string>());
    }

    void FileBlockCursorStore::save(const std::string &cursor_name, uint64_t block_number)
    {
        auto data = read_cursor_file(path_);
        if (data.contains(cursor_name))
        {
            uint64_t current = data[cursor_name].is_number_unsigned()
                                   ? data[cursor_name].get<uint64_t>()
                                   : evm_quantity_to_uint64(data[cursor_name].get<std::string>());
            if (block_number < current)
                block_number = current;
        }
        data[cursor_name] = block_number;

        auto parent = std::filesystem::path(path_).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        auto tmp_path = path_ + ".tmp";
        {
            std::ofstream output(tmp_path, std::ios::trunc);
            if (!output)
                throw std::runtime_error("failed to write cursor file: " + tmp_path);
            output << data.dump(2) << "\n";
        }
        std::filesystem::rename(tmp_path, path_);
    }

    EvmEventIndexer::EvmEventIndexer(EvmEventIndexerConfig config,
                                     std::shared_ptr<EvmBlockCursorStore> cursor_store)
        : config_(std::move(config)),
          cursor_store_(std::move(cursor_store)),
          http_(config_.rpc_http_url),
          ws_(config_.rpc_ws_url)
    {
        ws_.set_ping_interval_ms(config_.ws_ping_interval_ms);
    }

    EvmEventIndexer::~EvmEventIndexer()
    {
        stop();
    }

    void EvmEventIndexer::on_log(EvmIndexedLogCallback callback)
    {
        log_cb_ = std::move(callback);
    }

    void EvmEventIndexer::on_checkpoint(EvmIndexCheckpointCallback callback)
    {
        checkpoint_cb_ = std::move(callback);
    }

    void EvmEventIndexer::on_error(EvmRpcErrorCallback callback)
    {
        error_cb_ = std::move(callback);
    }

    EvmCatchUpReport EvmEventIndexer::catch_up()
    {
        uint64_t latest = evm_quantity_to_uint64(http_.block_number());
        uint64_t target = latest > config_.confirmations ? latest - config_.confirmations : 0;
        auto saved = cursor_store_ ? cursor_store_->load(config_.cursor_name) : std::nullopt;
        uint64_t from = saved ? *saved + 1 : config_.start_block;

        EvmCatchUpReport report{from, target, 0, 0};
        for (const auto &range : evm_make_block_ranges(from, target, config_.batch_size))
        {
            auto filter = config_.filter;
            filter.from_block = evm_uint64_to_quantity(range.from_block);
            filter.to_block = evm_uint64_to_quantity(range.to_block);
            auto logs = http_.get_logs(filter);
            for (const auto &log : logs)
            {
                report.logs_seen++;
                if (log_cb_)
                    log_cb_({log, false, received_at_ms()});
            }
            report.ranges_scanned++;
            save_checkpoint(range.to_block);
        }
        return report;
    }

    bool EvmEventIndexer::start_live()
    {
        auto live_filter = config_.filter;
        live_filter.from_block.clear();
        live_filter.to_block.clear();
        ws_.on_error([this](const std::string &error) { emit_error(error); });
        ws_.on_log([this](const EvmLog &log) { handle_live_log(log); });
        ws_.subscribe_logs(live_filter);
        return ws_.connect();
    }

    void EvmEventIndexer::run_live_for(std::chrono::seconds duration)
    {
        std::this_thread::sleep_for(duration);
        stop();
    }

    void EvmEventIndexer::stop()
    {
        ws_.stop();
    }

    void EvmEventIndexer::save_checkpoint(uint64_t block_number)
    {
        if (cursor_store_)
            cursor_store_->save(config_.cursor_name, block_number);
        if (checkpoint_cb_)
            checkpoint_cb_(block_number);
    }

    void EvmEventIndexer::emit_error(const std::string &message)
    {
        if (error_cb_)
            error_cb_(message);
    }

    void EvmEventIndexer::handle_live_log(const EvmLog &log)
    {
        try
        {
            if (log_cb_)
                log_cb_({log, true, received_at_ms()});
            if (!log.removed && !log.block_number.empty())
                save_checkpoint(evm_quantity_to_uint64(log.block_number));
        }
        catch (const std::exception &error)
        {
            emit_error(error.what());
        }
    }

} // namespace polymarket
