#include "order_execution.hpp"
#include "polymarket/clob_client.hpp"
#include "polymarket/decimal_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace polymarket::detail
{
    namespace
    {
        constexpr int depth_decimals = 6;
        constexpr std::uint64_t depth_scale = 1'000'000;
        constexpr auto uint64_max = std::numeric_limits<std::uint64_t>::max();

        struct AccumulatedNotional
        {
            std::uint64_t units{0};
            std::uint64_t fractional_units{0};
            bool saturated{false};

            bool covers(std::uint64_t target) const { return saturated || units >= target; }
        };

        struct AccumulatedSize
        {
            std::uint64_t units{0};
            bool saturated{false};

            bool covers(std::uint64_t target) const { return saturated || units >= target; }
        };

        struct LevelUnits
        {
            std::uint64_t price{0};
            std::uint64_t size{0};
        };

        struct DepthLimit
        {
            double price{0.0};
            bool fully_fillable{false};
        };

        std::uint64_t exact_depth_units(double value)
        {
            return exact_decimal_to_scaled_uint64(value, depth_decimals);
        }

        LevelUnits level_units(const PriceLevel &level)
        {
            if (!std::isfinite(level.price) || !std::isfinite(level.size) || level.price <= 0.0 ||
                level.price >= 1.0 || level.size < 0.0)
            {
                throw std::invalid_argument("orderbook level is invalid");
            }
            const auto size_units = exact_depth_units(level.size);
            const auto price_units = exact_depth_units(level.price);
            if (price_units == 0 || price_units >= depth_scale)
                throw std::invalid_argument("orderbook level is invalid");
            return {price_units, size_units};
        }

        void add_notional(AccumulatedNotional &total, std::uint64_t size_units,
                          std::uint64_t price_units)
        {
            if (total.saturated) return;

            const auto size_whole = size_units / depth_scale;
            const auto size_fraction = size_units % depth_scale;
            const auto fractional_product = size_fraction * price_units;
            const auto fractional_whole = fractional_product / depth_scale;
            const auto contribution_fraction = fractional_product % depth_scale;
            if (price_units != 0 && size_whole > (uint64_max - fractional_whole) / price_units)
            {
                total.saturated = true;
                return;
            }
            const auto contribution_units = size_whole * price_units + fractional_whole;
            if (contribution_units > uint64_max - total.units)
            {
                total.saturated = true;
                return;
            }
            total.units += contribution_units;
            total.fractional_units += contribution_fraction;
            if (total.fractional_units < depth_scale) return;

            total.fractional_units -= depth_scale;
            if (total.units == uint64_max)
            {
                total.saturated = true;
                return;
            }
            ++total.units;
        }

        void add_size(AccumulatedSize &total, std::uint64_t size_units)
        {
            if (total.saturated) return;
            if (size_units > uint64_max - total.units)
            {
                total.saturated = true;
                return;
            }
            total.units += size_units;
        }

        std::uint64_t checked_add(std::uint64_t left, std::uint64_t right)
        {
            if (right > uint64_max - left)
                throw std::out_of_range("simulated fill exceeds uint64 range");
            return left + right;
        }

        // Collateral for `share_units` at one price.
        std::uint64_t collateral_for(std::uint64_t share_units, std::uint64_t price_units,
                                     bool round_up)
        {
            const auto whole = share_units / depth_scale;
            const auto fractional_product = (share_units % depth_scale) * price_units;
            if (whole > (uint64_max - depth_scale) / price_units)
                throw std::out_of_range("simulated fill exceeds uint64 range");
            auto units = whole * price_units + fractional_product / depth_scale;
            if (round_up && fractional_product % depth_scale != 0) ++units;
            return units;
        }

        // Shares that `collateral_units` buys at one price, rounded down.
        std::uint64_t shares_for(std::uint64_t collateral_units, std::uint64_t price_units)
        {
            const auto whole = collateral_units / price_units;
            const auto remainder = collateral_units % price_units;
            if (whole > (uint64_max - depth_scale) / depth_scale)
                throw std::out_of_range("simulated fill exceeds uint64 range");
            return whole * depth_scale + remainder * depth_scale / price_units;
        }

        // Calls visit(level) from the best price outward until it returns true.
        // REST books list the best price last; OrderbookManager lists it first.
        template <typename Visit>
        void visit_best_first(const std::vector<PriceLevel> &levels, OrderSide side, Visit &&visit)
        {
            // Finite prices keep the comparison a strict weak ordering.
            for (const auto &level : levels)
            {
                if (!std::isfinite(level.price))
                    throw std::invalid_argument("orderbook level is invalid");
            }
            const auto better = [side](const PriceLevel &left, const PriceLevel &right)
            {
                return side == OrderSide::BUY ? left.price < right.price : left.price > right.price;
            };
            if (std::is_sorted(levels.begin(), levels.end(), better))
            {
                for (const auto &level : levels)
                    if (visit(level)) return;
                return;
            }
            if (std::is_sorted(levels.rbegin(), levels.rend(), better))
            {
                for (auto level = levels.rbegin(); level != levels.rend(); ++level)
                    if (visit(*level)) return;
                return;
            }
            auto sorted = levels;
            std::sort(sorted.begin(), sorted.end(), better);
            for (const auto &level : sorted)
                if (visit(level)) return;
        }

        // Finds the first level whose cumulative depth covers the order that
        // would be signed at that level's price, or the worst level otherwise.
        DepthLimit find_depth_limit(const Orderbook &book, OrderSide side, double amount,
                                    const std::string &tick_size)
        {
            if (!std::isfinite(amount) || amount <= 0.0)
            {
                throw std::invalid_argument("market order amount must be finite and positive");
            }
            const auto &levels = side == OrderSide::BUY ? book.asks : book.bids;
            if (levels.empty())
            {
                throw std::runtime_error("no matching orders");
            }

            AccumulatedNotional available_notional;
            AccumulatedSize available_size;
            DepthLimit limit;
            visit_best_first(levels, side,
                             [&](const PriceLevel &level)
                             {
                                 const auto units = level_units(level);
                                 const auto candidate_amounts = calculate_market_order_amounts(
                                     side, amount, validate_order_price(level.price, tick_size));
                                 if (side == OrderSide::BUY)
                                     add_notional(available_notional, units.size, units.price);
                                 else
                                     add_size(available_size, units.size);
                                 limit.price = level.price;
                                 limit.fully_fillable =
                                     side == OrderSide::BUY
                                         ? available_notional.covers(candidate_amounts.maker)
                                         : available_size.covers(candidate_amounts.maker);
                                 return limit.fully_fillable;
                             });
            return limit;
        }
    } // namespace

    double calculate_market_price(const Orderbook &book, OrderSide side, double amount,
                                  OrderType order_type, const std::string &tick_size)
    {
        const auto limit = find_depth_limit(book, side, amount, tick_size);
        if (!limit.fully_fillable && order_type == OrderType::FOK)
        {
            throw std::runtime_error("insufficient orderbook depth for FOK order");
        }
        return limit.price;
    }

    MarketPriceEstimate estimate_market_depth(const Orderbook &book, OrderSide side, double amount,
                                              const std::string &tick_size)
    {
        const auto limit = find_depth_limit(book, side, amount, tick_size);
        // Fill the amounts create_market_order would sign at the limit price:
        // a collateral budget for BUY, shares for SELL.
        const auto order = calculate_market_order_amounts(
            side, amount, validate_order_price(limit.price, tick_size));
        auto remaining = order.maker;

        MarketPriceEstimate estimate;
        estimate.price = limit.price;
        estimate.fully_fillable = limit.fully_fillable;
        const auto &levels = side == OrderSide::BUY ? book.asks : book.bids;
        visit_best_first(
            levels, side,
            [&](const PriceLevel &level)
            {
                if (side == OrderSide::BUY ? level.price > limit.price : level.price < limit.price)
                    return true;
                const auto units = level_units(level);
                std::uint64_t shares = 0;
                std::uint64_t collateral = 0;
                bool partial = false;
                if (side == OrderSide::BUY)
                {
                    const auto level_cost = collateral_for(units.size, units.price, true);
                    partial = remaining < level_cost;
                    shares = partial ? shares_for(remaining, units.price) : units.size;
                    collateral = partial ? collateral_for(shares, units.price, true) : level_cost;
                    remaining -= collateral;
                }
                else
                {
                    shares = std::min(remaining, units.size);
                    collateral = collateral_for(shares, units.price, false);
                    remaining -= shares;
                }
                if (shares != 0)
                {
                    ++estimate.levels;
                    estimate.share_units = checked_add(estimate.share_units, shares);
                    estimate.collateral_units = checked_add(estimate.collateral_units, collateral);
                }
                return partial || remaining == 0;
            });

        if (estimate.share_units != 0)
        {
            estimate.average_price = static_cast<double>(estimate.collateral_units) /
                                     static_cast<double>(estimate.share_units);
        }
        return estimate;
    }
} // namespace polymarket::detail
