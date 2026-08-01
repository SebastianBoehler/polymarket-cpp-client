#include "evm_event_indexer.hpp"
#include "evm_cursor_io.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace polymarket
{
    namespace
    {
        uint64_t read_quantity(const nlohmann::json &value)
        {
            return value.is_number_unsigned()
                       ? value.get<uint64_t>()
                       : evm_quantity_to_uint64(value.get<std::string>());
        }

        EvmIndexCursor cursor_from_json(const nlohmann::json &value)
        {
            if (!value.is_object())
                return {read_quantity(value), true, std::nullopt};

            EvmIndexCursor cursor;
            cursor.block_number = read_quantity(value.at("blockNumber"));
            cursor.block_complete = value.value("blockComplete", true);
            if (value.contains("lastLog"))
            {
                const auto &last = value.at("lastLog");
                cursor.last_log = EvmLogPosition{
                    read_quantity(last.at("transactionIndex")),
                    read_quantity(last.at("logIndex")),
                    last.value("blockHash", "")};
            }
            return cursor;
        }

        nlohmann::json cursor_to_json(const EvmIndexCursor &cursor)
        {
            nlohmann::json out = {
                {"blockNumber", cursor.block_number},
                {"blockComplete", cursor.block_complete}};
            if (cursor.last_log)
            {
                out["lastLog"] = {
                    {"transactionIndex", cursor.last_log->transaction_index},
                    {"logIndex", cursor.last_log->log_index},
                    {"blockHash", cursor.last_log->block_hash}};
            }
            return out;
        }

        bool cursor_advances(const EvmIndexCursor &current, const EvmIndexCursor &next)
        {
            if (next.block_number != current.block_number)
                return next.block_number > current.block_number;
            if (current.block_complete)
                return next.block_complete;
            if (next.block_complete)
                return true;
            if (!next.last_log)
                return false;
            if (!current.last_log)
                return true;
            if (next.last_log->block_hash != current.last_log->block_hash)
                return false;
            return next.last_log->transaction_index > current.last_log->transaction_index ||
                   (next.last_log->transaction_index == current.last_log->transaction_index &&
                    next.last_log->log_index >= current.last_log->log_index);
        }
    } // namespace

    uint64_t evm_quantity_to_uint64(const std::string &quantity)
    {
        if (quantity.empty())
            throw std::invalid_argument("empty EVM quantity");
        std::string_view digits(quantity);
        int base = 10;
        if (digits.size() >= 2 && digits[0] == '0' &&
            (digits[1] == 'x' || digits[1] == 'X'))
        {
            digits.remove_prefix(2);
            base = 16;
        }
        if (digits.empty())
            throw std::invalid_argument("EVM quantity has no digits");

        uint64_t value = 0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(),
                                            value, base);
        if (result.ec == std::errc::result_out_of_range)
            throw std::out_of_range("EVM quantity exceeds uint64");
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
            throw std::invalid_argument("invalid EVM quantity");
        return value;
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
            const auto to = from + std::min(batch_size - 1, to_block - from);
            ranges.push_back({from, to});
            if (to == to_block)
                break;
            from = to + 1;
        }
        return ranges;
    }

    std::optional<EvmIndexCursor> EvmBlockCursorStore::load_cursor(const std::string &name)
    {
        auto block = load(name);
        return block ? std::optional<EvmIndexCursor>({*block, true, std::nullopt}) : std::nullopt;
    }

    void EvmBlockCursorStore::save_cursor(const std::string &name, const EvmIndexCursor &cursor)
    {
        if (cursor.block_complete)
            save(name, cursor.block_number);
        else if (cursor.block_number > 0)
            save(name, cursor.block_number - 1);
    }

    void EvmBlockCursorStore::rewind(const std::string &name, uint64_t from_block)
    {
        (void)name;
        (void)from_block;
        throw std::runtime_error("cursor store does not implement reorg rewind");
    }

    FileBlockCursorStore::FileBlockCursorStore(std::string path)
        : path_(detail::canonical_cursor_path(path)),
          mutex_(detail::cursor_path_mutex(path_))
    {
    }

    std::optional<uint64_t> FileBlockCursorStore::load(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        const auto data = detail::read_cursor_file(path_);
        if (!data.contains(name))
            return std::nullopt;
        const auto cursor = cursor_from_json(data.at(name));
        if (cursor.block_complete)
            return cursor.block_number;
        return cursor.block_number == 0 ? std::nullopt
                                        : std::optional<uint64_t>(cursor.block_number - 1);
    }

    void FileBlockCursorStore::save(const std::string &name, uint64_t block_number)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        auto data = detail::read_cursor_file(path_);
        if (data.contains(name) && block_number < cursor_from_json(data.at(name)).block_number)
            return;
        data[name] = cursor_to_json({block_number, true, std::nullopt});
        detail::write_cursor_file(path_, data);
    }

    std::optional<EvmIndexCursor> FileBlockCursorStore::load_cursor(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        auto data = detail::read_cursor_file(path_);
        return data.contains(name) ? std::optional<EvmIndexCursor>(cursor_from_json(data.at(name)))
                                   : std::nullopt;
    }

    void FileBlockCursorStore::save_cursor(const std::string &name,
                                           const EvmIndexCursor &cursor)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        auto data = detail::read_cursor_file(path_);
        if (data.contains(name) && !cursor_advances(cursor_from_json(data.at(name)), cursor))
            return;
        data[name] = cursor_to_json(cursor);
        detail::write_cursor_file(path_, data);
    }

    void FileBlockCursorStore::rewind(const std::string &name, uint64_t from_block)
    {
        std::lock_guard<std::mutex> lock(*mutex_);
        auto data = detail::read_cursor_file(path_);
        if (!data.contains(name))
            return;
        const auto current = cursor_from_json(data.at(name));
        if (from_block > current.block_number)
            return;
        data[name] = cursor_to_json({from_block, false, std::nullopt});
        detail::write_cursor_file(path_, data);
    }
} // namespace polymarket
