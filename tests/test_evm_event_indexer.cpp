#include "evm_event_indexer.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main()
{
    using namespace polymarket;

    require(evm_quantity_to_uint64("0x10") == 16, "hex quantity parse failed");
    require(evm_quantity_to_uint64("42") == 42, "decimal quantity parse failed");
    require(evm_uint64_to_quantity(0) == "0x0", "zero quantity format failed");
    require(evm_uint64_to_quantity(255) == "0xff", "quantity format failed");

    auto ranges = evm_make_block_ranges(10, 25, 8);
    require(ranges.size() == 2, "range count failed");
    require(ranges[0].from_block == 10, "first range start failed");
    require(ranges[0].to_block == 17, "first range end failed");
    require(ranges[1].from_block == 18, "second range start failed");
    require(ranges[1].to_block == 25, "second range end failed");
    require(evm_make_block_ranges(30, 20, 8).empty(), "empty range failed");

    const auto path = std::filesystem::temp_directory_path() /
                      "polymarket_cpp_client_cursor_test.json";
    std::filesystem::remove(path);

    {
        FileBlockCursorStore store(path.string());
        require(!store.load("ctf"), "empty cursor should not load");
        store.save("ctf", 12345);
        store.save("uma", 77);
        require(store.load("ctf").value() == 12345, "cursor save failed");
    }

    {
        FileBlockCursorStore store(path.string());
        require(store.load("ctf").value() == 12345, "cursor reload failed");
        require(store.load("uma").value() == 77, "second cursor reload failed");
        store.save("ctf", 12346);
        require(store.load("ctf").value() == 12346, "cursor update failed");
        store.save("ctf", 12);
        require(store.load("ctf").value() == 12346, "cursor regressed");
    }

    std::filesystem::remove(path);
    std::cout << "test_evm_event_indexer passed\n";
    return 0;
}
