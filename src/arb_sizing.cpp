#include "orderbook.hpp"

#include <algorithm>
#include <cmath>

namespace polymarket
{
    namespace
    {
        constexpr double share_step = 0.01;
        constexpr double minimum_edge_usdc = 0.01;

        double round_up_to_tick(double value, double tick)
        {
            return std::ceil(value / tick - 1e-12) * tick;
        }

        double worst_fee_price(double ask, double limit, int exponent)
        {
            return exponent == 0 ? ask : std::clamp(0.5, ask, limit);
        }

        double fee_per_share(double ask, double limit, double rate, int exponent)
        {
            const double price = worst_fee_price(ask, limit, exponent);
            return rate * std::pow(price * (1.0 - price), exponent);
        }

        double conservative_fee(double shares, double per_share)
        {
            return std::ceil(shares * per_share * 100000.0 - 1e-12) / 100000.0;
        }
    }

    ArbExecutionPlan size_complementary_arb(const ArbSizingInput &input)
    {
        ArbExecutionPlan plan;
        const bool finite = std::isfinite(input.yes_ask) && std::isfinite(input.no_ask) &&
                            std::isfinite(input.yes_available) && std::isfinite(input.no_available) &&
                            std::isfinite(input.max_usdc_per_leg) && std::isfinite(input.slippage) &&
                            std::isfinite(input.tick_size) && std::isfinite(input.minimum_order_size) &&
                            std::isfinite(input.fee_rate);
        if (!finite || input.yes_ask <= 0.0 || input.yes_ask >= 1.0 ||
            input.no_ask <= 0.0 || input.no_ask >= 1.0 ||
            input.yes_available < 0.0 || input.no_available < 0.0 ||
            input.minimum_order_size < 0.0 || input.max_usdc_per_leg <= 0.0 ||
            input.tick_size <= 0.0 || input.slippage < 0.0 ||
            input.fee_rate < 0.0 || input.fee_exponent < 0)
        {
            plan.reason = "invalid sizing input";
            return plan;
        }
        plan.yes_limit_price = round_up_to_tick(input.yes_ask + input.slippage, input.tick_size);
        plan.no_limit_price = round_up_to_tick(input.no_ask + input.slippage, input.tick_size);
        if (!std::isfinite(plan.yes_limit_price) || !std::isfinite(plan.no_limit_price) ||
            plan.yes_limit_price >= 1.0 || plan.no_limit_price >= 1.0)
        {
            plan.reason = "limit price outside market range";
            return plan;
        }
        const double yes_fee_per_share = fee_per_share(
            input.yes_ask, plan.yes_limit_price, input.fee_rate, input.fee_exponent);
        const double no_fee_per_share = fee_per_share(
            input.no_ask, plan.no_limit_price, input.fee_rate, input.fee_exponent);
        const double max_unit_cost = std::max(plan.yes_limit_price + yes_fee_per_share,
                                              plan.no_limit_price + no_fee_per_share);
        const double raw_share_steps = input.max_usdc_per_leg / max_unit_cost * 100.0;
        if (!std::isfinite(yes_fee_per_share) || !std::isfinite(no_fee_per_share) ||
            !std::isfinite(max_unit_cost) || max_unit_cost <= 0.0 ||
            !std::isfinite(raw_share_steps))
        {
            plan.reason = "sizing overflow";
            return plan;
        }
        double shares = std::floor(raw_share_steps + 1e-12) / 100.0;
        while (shares > 0.0)
        {
            const double yes_cost = shares * plan.yes_limit_price +
                                    conservative_fee(shares, yes_fee_per_share);
            const double no_cost = shares * plan.no_limit_price +
                                   conservative_fee(shares, no_fee_per_share);
            if (!std::isfinite(yes_cost) || !std::isfinite(no_cost))
            {
                plan.reason = "sizing overflow";
                return plan;
            }
            if (yes_cost <= input.max_usdc_per_leg + 1e-12 &&
                no_cost <= input.max_usdc_per_leg + 1e-12)
            {
                break;
            }
            const double reduced = std::max(0.0, shares - share_step);
            if (!(reduced < shares))
            {
                plan.reason = "sizing overflow";
                return plan;
            }
            shares = reduced;
        }
        plan.yes_shares = shares;
        plan.no_shares = shares;
        if (shares <= 0.0 || shares < input.minimum_order_size)
        {
            plan.reason = "below minimum order size";
            return plan;
        }
        if (input.yes_available < shares || input.no_available < shares)
        {
            plan.reason = "insufficient top-level depth";
            return plan;
        }
        plan.yes_fee = conservative_fee(shares, yes_fee_per_share);
        plan.no_fee = conservative_fee(shares, no_fee_per_share);
        plan.total_cost = shares * (plan.yes_limit_price + plan.no_limit_price) + plan.yes_fee + plan.no_fee;
        plan.edge = shares - plan.total_cost;
        if (!std::isfinite(plan.yes_fee) || !std::isfinite(plan.no_fee) ||
            !std::isfinite(plan.total_cost) || !std::isfinite(plan.edge))
        {
            plan = {};
            plan.reason = "sizing overflow";
            return plan;
        }
        plan.executable = plan.edge > minimum_edge_usdc;
        plan.reason = plan.executable ? "" : "executable edge does not clear safety buffer";
        return plan;
    }

    bool has_fresh_arb_depth(const MarketState &market, uint64_t now, uint64_t max_age,
                             double required_shares)
    {
        if (!std::isfinite(market.best_ask_yes) || !std::isfinite(market.best_ask_no) ||
            !std::isfinite(market.best_ask_yes_size) ||
            !std::isfinite(market.best_ask_no_size) ||
            !std::isfinite(required_shares) || required_shares < 0.0 ||
            market.best_ask_yes <= 0.0 || market.best_ask_yes >= 1.0 ||
            market.best_ask_no <= 0.0 || market.best_ask_no >= 1.0 ||
            market.last_update_yes_ns == 0 || market.last_update_no_ns == 0 ||
            market.best_ask_yes_size < required_shares || market.best_ask_no_size < required_shares)
        {
            return false;
        }
        const auto fresh = [now, max_age](uint64_t timestamp)
        { return timestamp <= now && now - timestamp <= max_age; };
        return fresh(market.last_update_yes_ns) && fresh(market.last_update_no_ns);
    }
}
