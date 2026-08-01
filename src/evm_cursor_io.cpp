#include "evm_cursor_io.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace polymarket::detail
{
    std::string canonical_cursor_path(const std::string &path)
    {
        return std::filesystem::weakly_canonical(
                   std::filesystem::absolute(std::filesystem::path(path)))
            .string();
    }

    std::shared_ptr<std::mutex> cursor_path_mutex(const std::string &canonical_path)
    {
        static std::mutex registry_mutex;
        static std::unordered_map<std::string, std::weak_ptr<std::mutex>> registry;
        std::lock_guard<std::mutex> lock(registry_mutex);
        auto &entry = registry[canonical_path];
        auto mutex = entry.lock();
        if (!mutex)
        {
            mutex = std::make_shared<std::mutex>();
            entry = mutex;
        }
        return mutex;
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

    void write_cursor_file(const std::string &path, const nlohmann::json &data)
    {
        auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        std::string pattern = path + ".tmp.XXXXXX";
        std::vector<char> temporary(pattern.begin(), pattern.end());
        temporary.push_back('\0');
        int fd = ::mkstemp(temporary.data());
        if (fd < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "failed to create cursor temporary file");
        const std::string temporary_path(temporary.data());
        const auto discard = [&]()
        {
            if (fd >= 0)
                ::close(fd);
            ::unlink(temporary_path.c_str());
        };

        try
        {
            const auto payload = data.dump(2) + "\n";
            std::size_t written = 0;
            while (written < payload.size())
            {
                const auto count = ::write(fd, payload.data() + written,
                                           payload.size() - written);
                if (count < 0)
                {
                    if (errno == EINTR)
                        continue;
                    throw std::system_error(errno, std::generic_category(),
                                            "failed to write cursor temporary file");
                }
                written += static_cast<std::size_t>(count);
            }
            if (::fsync(fd) != 0)
                throw std::system_error(errno, std::generic_category(),
                                        "failed to sync cursor temporary file");
            const auto close_result = ::close(fd);
            fd = -1;
            if (close_result != 0)
                throw std::system_error(errno, std::generic_category(),
                                        "failed to close cursor temporary file");
            if (::rename(temporary_path.c_str(), path.c_str()) != 0)
                throw std::system_error(errno, std::generic_category(),
                                        "failed to replace cursor file");

            const auto directory = parent.empty() ? std::filesystem::path(".") : parent;
            const int directory_fd = ::open(directory.c_str(), O_RDONLY);
            if (directory_fd >= 0)
            {
                (void)::fsync(directory_fd);
                ::close(directory_fd);
            }
        }
        catch (...)
        {
            discard();
            throw;
        }
    }
}
