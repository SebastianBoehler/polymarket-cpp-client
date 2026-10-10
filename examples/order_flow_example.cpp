#include "example_environment.hpp"
#include "polymarket/clob_client.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// Preview, place and settle orders with the order-flow helpers.
//
//   order_flow_example market <token_id> <usd_amount> [--execute]
//   order_flow_example limit <token_id> <price> <size> [--execute]
//
// market: estimates a BUY of <usd_amount> from the live book. With --execute
// it places a FAK BUY bounded at the estimated worst price, so it never fills
// worse than the preview, then waits until its fills settle on-chain.
//
// limit: places a post-only GTD BUY that expires in one hour, so it rests on
// the book or is rejected; it never matches on arrival. Only with --execute.
//
// Without --execute nothing is signed or sent. Environment: PRIVATE_KEY is
// required for --execute. With POLYMARKET_PROXY_ADDRESS set, orders are made
// for that Gnosis Safe. POLYMARKET_ENV selects the deployment (default
// production). Orders placed with --execute use real funds.

namespace
{
    std::string env(const char *name)
    {
        const char *value = std::getenv(name);
        return value ? value : "";
    }

    int usage()
    {
        std::cerr << "usage: order_flow_example market <token_id> <usd_amount> [--execute]\n"
                     "       order_flow_example limit <token_id> <price> <size> [--execute]\n";
        return 2;
    }

    void print_error(const char *step, const polymarket::SdkError &error)
    {
        std::cerr << step << " failed: " << polymarket::sdk_error_code_to_string(error.code) << ' '
                  << error.message << '\n';
    }

    std::uint64_t unix_now_seconds()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now).count());
    }
} // namespace

int main(int argc, char **argv)
{
    using namespace polymarket;

    bool execute = false;
    if (argc > 1 && std::strcmp(argv[argc - 1], "--execute") == 0)
    {
        execute = true;
        --argc;
    }
    if (argc < 2) return usage();
    const std::string mode = argv[1];
    if (!((mode == "market" && argc == 4) || (mode == "limit" && argc == 5))) return usage();
    const std::string token_id = argv[2];

    try
    {
        http_global_init();
        const auto environment = polymarket_example::selected_environment();
        const bool market = mode == "market";
        double amount = 0.0;
        double worst_price = 0.0;

        if (market)
        {
            amount = std::stod(argv[3]);
            ClobClient public_client(environment);
            const auto estimate = public_client.estimate_market_price(token_id, OrderSide::BUY,
                                                                      amount, OrderType::FAK);
            if (!estimate)
            {
                print_error("estimate", estimate.error());
                return 1;
            }
            const auto &value = estimate.value();
            std::cout << "BUY $" << amount << ": worst price " << value.price << ", average "
                      << value.average_price << ", " << value.share_units / 1e6 << " shares across "
                      << value.levels << " level(s)"
                      << (value.fully_fillable ? "" : " (book too shallow; FAK fills part)")
                      << '\n';
            worst_price = value.price;
        }
        else
        {
            std::cout << "post-only GTD BUY " << argv[4] << " shares @ " << argv[3]
                      << ", expiring in one hour\n";
        }
        if (!execute)
        {
            std::cout << "dry run: pass --execute to place the order\n";
            return 0;
        }

        const auto private_key = env("PRIVATE_KEY");
        if (private_key.empty())
        {
            std::cerr << "PRIVATE_KEY is required for --execute\n";
            return 1;
        }
        const auto funder = env("POLYMARKET_PROXY_ADDRESS");
        const auto signature_type =
            funder.empty() ? SignatureType::EOA : SignatureType::POLY_GNOSIS_SAFE;
        // Derive L2 API credentials from the signer (no orders are placed).
        const auto credentials =
            ClobClient(environment, private_key, signature_type, funder).create_or_derive_api_key();
        ClobClient client(environment, private_key, credentials, signature_type, funder);

        const auto geoblock = client.get_geoblock_status();
        if (!geoblock || geoblock.value().blocked)
        {
            std::cerr << "order placement is not available from this location\n";
            return 1;
        }

        const auto placed = [&]
        {
            if (market)
            {
                PlaceMarketOrderParams order;
                order.token_id = token_id;
                order.side = OrderSide::BUY;
                order.amount = amount;
                order.worst_price = worst_price; // never fill worse than the preview
                return client.place_market_order(order);
            }
            PlaceLimitOrderParams order;
            order.token_id = token_id;
            order.price = std::stod(argv[3]);
            order.size = std::stod(argv[4]);
            order.side = OrderSide::BUY;
            order.post_only = true;
            order.expiration = unix_now_seconds() + 3600;
            return client.place_limit_order(order);
        }();
        if (!placed)
        {
            print_error("place order", placed.error());
            return 1;
        }
        std::cout << "order " << placed.value().order_id << ' ' << placed.value().status << ", "
                  << placed.value().trade_ids.size() << " fill(s)\n";

        const auto settlement = client.wait_for_order_fill_settlement(placed.value());
        if (!settlement)
        {
            print_error("settlement", settlement.error());
            return 1;
        }
        for (const auto &trade : settlement.value().trades)
            std::cout << "fill " << trade.id << ' ' << trade.status << ' ' << trade.size << " @ "
                      << trade.price << '\n';
        for (const auto &hash : settlement.value().transaction_hashes)
            std::cout << "settled in " << hash << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
