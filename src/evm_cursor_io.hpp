#pragma once

#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

namespace polymarket::detail
{
    std::string canonical_cursor_path(const std::string &path);
    std::shared_ptr<std::mutex> cursor_path_mutex(const std::string &canonical_path);
    nlohmann::json read_cursor_file(const std::string &path);
    void write_cursor_file(const std::string &path, const nlohmann::json &data);
}
