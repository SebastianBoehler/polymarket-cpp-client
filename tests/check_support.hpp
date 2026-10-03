#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>

// Minimal assertion helpers shared by the EVM and position tests. Failures are
// counted rather than aborting so one run reports every mismatch.
namespace check_support
{
    inline int failures = 0;

    inline void check(bool condition, const std::string &message)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << message << "\n";
    }

    inline void expect_equal(const std::string &name, const std::string &actual, const std::string &expected)
    {
        check(actual == expected, name + " mismatch\n  expected: " + expected + "\n  actual:   " + actual);
    }

    template <typename Error>
    void expect_throws(const std::string &name, const std::function<void()> &action)
    {
        try
        {
            action();
        }
        catch (const Error &)
        {
            return;
        }
        catch (const std::exception &error)
        {
            check(false, name + " threw the wrong exception type: " + error.what());
            return;
        }
        check(false, name + " did not throw");
    }

    inline int finish(const std::string &test_name)
    {
        if (failures != 0)
        {
            std::cerr << failures << ' ' << test_name << " check(s) failed\n";
            return 1;
        }
        std::cout << test_name << " passed\n";
        return 0;
    }
} // namespace check_support
