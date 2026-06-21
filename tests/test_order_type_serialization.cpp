#include "http_client.hpp"
#include "order_signer.hpp"
#include "sdk_error.hpp"
#include "types.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#define private public
#include "clob_client.hpp"
#undef private

using namespace polymarket;

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
}

int main()
{
    ClobClient client("https://clob.polymarket.com");

    if (!expect_equal("GTC order type", client.order_type_to_string(OrderType::GTC), "GTC") ||
        !expect_equal("GTD order type", client.order_type_to_string(OrderType::GTD), "GTD") ||
        !expect_equal("FOK order type", client.order_type_to_string(OrderType::FOK), "FOK") ||
        !expect_equal("FAK order type", client.order_type_to_string(OrderType::FAK), "FAK"))
    {
        return 1;
    }

    return 0;
}
