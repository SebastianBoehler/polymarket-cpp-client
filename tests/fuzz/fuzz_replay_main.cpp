#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

// Replays saved fuzz inputs through a fuzz target without libFuzzer, so the
// corpus runs with any compiler. Arguments are input files or directories.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);

namespace
{
    bool replay_file(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            std::cerr << "cannot read " << path << '\n';
            return false;
        }
        const std::string bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
        (void)LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t *>(bytes.data()),
                                     bytes.size());
        return true;
    }
} // namespace

int main(int argc, char **argv)
{
    std::size_t replayed = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::filesystem::path path(argv[index]);
        std::vector<std::filesystem::path> files;
        if (std::filesystem::is_directory(path))
        {
            for (const auto &entry : std::filesystem::directory_iterator(path))
            {
                if (entry.is_regular_file()) files.push_back(entry.path());
            }
        }
        else
        {
            files.push_back(path);
        }
        for (const auto &file : files)
        {
            if (!replay_file(file)) return 1;
            ++replayed;
        }
    }
    if (replayed == 0)
    {
        std::cerr << "no fuzz inputs replayed\n";
        return 1;
    }
    std::cout << "replayed " << replayed << " fuzz inputs\n";
    return 0;
}
