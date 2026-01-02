#include "order_signer.hpp"
#include <cassert>
#include <iostream>

int main()
{
    using namespace polymarket;

    // Test to_wei conversion
    auto wei = to_wei(1.23, 6);
    assert(wei == "1230000");

    // Test salt generation is non-empty
    auto salt = generate_salt();
    assert(!salt.empty());

    std::cout << "test_utils passed\n";
    return 0;
}
