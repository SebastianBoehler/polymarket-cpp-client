#include "arb_runtime.hpp"
#include "orderbook.hpp"

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace polymarket;

namespace
{
    bool expect_near(const char *name, double actual, double expected)
    {
        if (std::abs(actual - expected) < 1e-8)
        {
            return true;
        }
        std::cerr << name << ": expected " << expected << ", got " << actual << '\n';
        return false;
    }
}

int main()
{
    ArbSizingInput input;
    input.yes_ask = 0.45;
    input.no_ask = 0.48;
    input.yes_available = 11.0;
    input.no_available = 20.0;
    input.max_usdc_per_leg = 5.0;
    input.slippage = 0.01;
    input.tick_size = 0.01;
    input.minimum_order_size = 5.0;
    input.fee_rate = 0.07;
    input.fee_exponent = 1;

    const auto plan = size_complementary_arb(input);
    if (!plan.executable ||
        !expect_near("yes rounded limit", plan.yes_limit_price, 0.46) ||
        !expect_near("no rounded limit", plan.no_limit_price, 0.49) ||
        !expect_near("fee-inclusive equal yes shares", plan.yes_shares, 9.85) ||
        !expect_near("fee-inclusive equal no shares", plan.no_shares, 9.85) ||
        !expect_near("yes conservative fee", plan.yes_fee, 0.17128) ||
        !expect_near("no conservative fee", plan.no_fee, 0.17231) ||
        !expect_near("executable total cost", plan.total_cost, 9.70109) ||
        !expect_near("executable edge", plan.edge, 0.14891) ||
        plan.yes_shares * plan.yes_limit_price + plan.yes_fee > input.max_usdc_per_leg ||
        plan.no_shares * plan.no_limit_price + plan.no_fee > input.max_usdc_per_leg)
    {
        return 1;
    }

    input.yes_ask = 0.48;
    input.no_ask = 0.48;
    const auto fee_negative = size_complementary_arb(input);
    if (fee_negative.executable || fee_negative.edge >= 0.0)
    {
        std::cerr << "fees must invalidate a raw-price-only opportunity\n";
        return 1;
    }

    input.yes_ask = 0.45;
    input.no_ask = 0.48;
    input.no_available = 9.84;
    const auto too_shallow = size_complementary_arb(input);
    if (too_shallow.executable || too_shallow.reason != "insufficient top-level depth")
    {
        std::cerr << "top-level size must cover both equal-share legs\n";
        return 1;
    }

    input.yes_ask = 0.51;
    input.no_ask = 0.40;
    input.yes_available = 20.0;
    input.no_available = 20.0;
    input.slippage = 0.04;
    input.fee_rate = 0.25;
    input.fee_exponent = 2;
    const auto above_midpoint = size_complementary_arb(input);
    const double fee_at_limit = above_midpoint.yes_shares * input.fee_rate *
                                std::pow(above_midpoint.yes_limit_price *
                                             (1.0 - above_midpoint.yes_limit_price),
                                         input.fee_exponent);
    if (above_midpoint.yes_shares * above_midpoint.yes_limit_price +
            above_midpoint.yes_fee > input.max_usdc_per_leg + 1e-12 ||
        above_midpoint.yes_fee <= fee_at_limit)
    {
        std::cerr << "fees must fit the leg budget and use the worst fill price\n";
        return 1;
    }

    input.yes_ask = 0.4997;
    input.no_ask = 0.4998;
    input.slippage = 0.0;
    input.tick_size = 0.0001;
    input.fee_rate = 0.0;
    const auto dust_edge = size_complementary_arb(input);
    if (dust_edge.executable || dust_edge.edge <= 0.0)
    {
        std::cerr << "sub-cent theoretical edges must retain a safety buffer\n";
        return 1;
    }

    input.yes_available = std::numeric_limits<double>::quiet_NaN();
    const auto unknown_depth = size_complementary_arb(input);
    if (unknown_depth.executable || unknown_depth.reason != "invalid sizing input")
    {
        std::cerr << "non-finite sizing input must fail closed\n";
        return 1;
    }

    input.yes_available = 1e308;
    input.no_available = 1e308;
    input.max_usdc_per_leg = 1e308;
    const auto overflow_start = std::chrono::steady_clock::now();
    const auto overflowed = size_complementary_arb(input);
    const auto overflow_elapsed = std::chrono::steady_clock::now() - overflow_start;
    if (overflowed.executable || overflowed.reason != "sizing overflow" ||
        overflow_elapsed > std::chrono::milliseconds(100))
    {
        std::cerr << "overflowed sizing intermediates must fail promptly\n";
        return 1;
    }

    if (arb::seconds_until(1'000, 2'000) != -1 ||
        arb::seconds_until(61'000, 1'000) != 60)
    {
        std::cerr << "expiry TTL must remain signed\n";
        return 1;
    }

    OrderResponse matched_yes{};
    matched_yes.success = true;
    matched_yes.status = "matched";
    OrderResponse matched_no = matched_yes;
    OrderResponse rejected{};
    rejected.success = false;
    rejected.error_msg = "FOK_ORDER_NOT_FILLED_ERROR";
    OrderResponse delayed{};
    delayed.success = true;
    delayed.status = "delayed";
    if (arb::assess_batch({matched_yes, matched_no}) != arb::BatchOutcome::BothFilled ||
        arb::assess_batch({rejected, rejected}) != arb::BatchOutcome::BothRejected ||
        arb::assess_batch({matched_yes, rejected}) != arb::BatchOutcome::Unsafe ||
        arb::assess_batch({delayed, delayed}) != arb::BatchOutcome::Unsafe ||
        arb::assess_batch({matched_yes}) != arb::BatchOutcome::Unsafe)
    {
        std::cerr << "batch result classification is not conservative\n";
        return 1;
    }

    MarketState metadata;
    if (!arb::reconcile_order_metadata(metadata, {"0.001"}, {"0.001"}, {true}, {true}) ||
        metadata.minimum_tick_size != "0.001" || !metadata.neg_risk)
    {
        std::cerr << "matching live order metadata must be applied\n";
        return 1;
    }
    if (arb::reconcile_order_metadata(metadata, {"0.001"}, {"0.01"}, {true}, {true}) ||
        arb::reconcile_order_metadata(metadata, {"0.001"}, {"0.001"}, {true}, {false}))
    {
        std::cerr << "complementary legs with inconsistent metadata must be rejected\n";
        return 1;
    }

    char executable[] = "polymarket_arb";
    char live_option[] = "--live";
    char *arguments[] = {executable, live_option};
    bool help_requested = false;
    bool live_rejected = false;
    try
    {
        (void)arb::parse_options(2, arguments, help_requested);
    }
    catch (const std::invalid_argument &error)
    {
        live_rejected = std::string(error.what()).find("not atomic") != std::string::npos;
    }
    if (!live_rejected)
    {
        std::cerr << "polymarket_arb --live must fail before authentication or I/O\n";
        return 1;
    }

    const char *existing_size = std::getenv("SIZE_USDC");
    const std::optional<std::string> previous_size =
        existing_size ? std::optional<std::string>(existing_size) : std::nullopt;
    unsetenv("SIZE_USDC");
    const auto rejects_options = [](std::vector<std::string> arguments)
    {
        std::vector<char *> argv;
        for (auto &argument : arguments) argv.push_back(argument.data());
        bool help = false;
        try
        {
            return !arb::parse_options(static_cast<int>(argv.size()), argv.data(), help);
        }
        catch (const std::exception &)
        {
            return true;
        }
    };
    const bool bad_argv_rejected =
        rejects_options({"polymarket_arb", "--max", "1junk"}) &&
        rejects_options({"polymarket_arb", "--max", "2147483648"}) &&
        rejects_options({"polymarket_arb", "--trigger", "nan"}) &&
        rejects_options({"polymarket_arb", "--trigger", "inf"}) &&
        rejects_options({"polymarket_arb", "--trigger", "0.9junk"});
    bool bad_environment_rejected = true;
    for (const char *value : {"nan", "inf", "5junk", "1e999"})
    {
        setenv("SIZE_USDC", value, 1);
        bad_environment_rejected = bad_environment_rejected &&
                                   rejects_options({"polymarket_arb"});
    }
    if (previous_size) setenv("SIZE_USDC", previous_size->c_str(), 1);
    else unsetenv("SIZE_USDC");
    if (!bad_argv_rejected || !bad_environment_rejected)
    {
        std::cerr << "CLI and environment numerics must be finite, exact values\n";
        return 1;
    }

    return 0;
}
