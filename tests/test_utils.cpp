#include "order_signer.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    bool expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        if (actual == expected)
        {
            return true;
        }

        std::cerr << name << " mismatch\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual << "\n";
        return false;
    }

    bool expect_throws(const std::string &name, const std::function<void()> &action)
    {
        try
        {
            action();
        }
        catch (const std::exception &)
        {
            return true;
        }
        std::cerr << "failed: " << name << " did not throw\n";
        return false;
    }
}

int main()
{
    using namespace polymarket;

    // Test to_wei conversion
    if (!expect_equal("basic wei conversion", to_wei(1.23, 6), "1230000"))
    {
        return 1;
    }

    auto shares = 3.0 / (1.0 - 0.01);
    if (!expect_equal("six decimal truncation", to_wei(shares, 6), "3030303"))
    {
        return 1;
    }

    auto rounded_shares = std::round(shares * 100.0) / 100.0;
    if (!expect_equal("rounded shares", to_wei(rounded_shares, 6), "3030000"))
    {
        return 1;
    }

    auto rounded_artifact = std::nextafter(3.03, 0.0);
    if (!expect_equal("rounded artifact", to_wei(rounded_artifact, 6), "3030000"))
    {
        return 1;
    }

    const double genuinely_sub_micro = 0.0000009999999995;
    if (!expect_equal("round down never crosses a true scaled-unit boundary",
                      to_wei(genuinely_sub_micro, 6), "0"))
    {
        return 1;
    }

    if (!expect_equal("round down", to_wei(1.2345678, 6), "1234567") ||
        !expect_equal("round nearest", to_wei(1.2345678, 6, false), "1234568"))
    {
        return 1;
    }

    if (!expect_equal("0.29 boundary", to_wei(0.29, 2), "29") ||
        !expect_equal("0.57 boundary", to_wei(0.57, 2), "57") ||
        !expect_equal("0.58 boundary", to_wei(0.58, 2), "58") ||
        !expect_equal("5.29 boundary", to_wei(5.29, 2), "529") ||
        !expect_throws("scaled uint64 exclusive upper bound", []
                       { (void)to_wei(18446744073709.551, 6); }) ||
        !expect_throws("infinite decimal", []
                       { (void)to_wei(std::numeric_limits<double>::infinity(), 6); }))
    {
        return 1;
    }

    // Test salt generation is non-empty
    auto salt = generate_salt();
    if (salt.empty())
    {
        std::cerr << "salt generation failed\n";
        return 1;
    }

    std::cout << "test_utils passed\n";
    return 0;
}
