#include "arb/signal_detector.hpp"
#include "arb/price_utils.hpp"
#include <utility>

namespace polymarket::arb
{

    namespace
    {
        uint64_t build_snapshot_key(const Orderbook &yes_book,
                                    const Orderbook &no_book,
                                    double adjusted_yes_price,
                                    double adjusted_no_price)
        {
            const auto yes_component = yes_book.timestamp_ns ^ (yes_book.timestamp_ns >> 17);
            const auto no_component = no_book.timestamp_ns ^ (no_book.timestamp_ns >> 11);
            const auto price_component =
                (static_cast<uint64_t>(adjusted_yes_price * 10000.0) << 32) ^
                static_cast<uint64_t>(adjusted_no_price * 10000.0);
            return yes_component ^ (no_component << 1) ^ price_component;
        }
    } // namespace

    SignalDetector::SignalDetector(ArbConfig config)
        : config_(std::move(config))
    {
    }

    std::optional<OpportunitySignal> SignalDetector::detect(const PreparedMarket &market,
                                                            const Orderbook &yes_book,
                                                            const Orderbook &no_book) const
    {
        const uint64_t detected_at_ns = now_ns();
        const auto max_age_ns = config_.max_book_age_ms * 1000000ULL;
        if (detected_at_ns - yes_book.timestamp_ns > max_age_ns ||
            detected_at_ns - no_book.timestamp_ns > max_age_ns)
        {
            return std::nullopt;
        }

        const double raw_best_ask_yes = yes_book.best_ask();
        const double raw_best_ask_no = no_book.best_ask();
        if (raw_best_ask_yes <= 0.0 || raw_best_ask_no <= 0.0 ||
            raw_best_ask_yes >= 1.0 || raw_best_ask_no >= 1.0)
        {
            return std::nullopt;
        }

        const double visible_depth_yes =
            best_ask_notional(raw_best_ask_yes, yes_book.best_ask_size());
        const double visible_depth_no =
            best_ask_notional(raw_best_ask_no, no_book.best_ask_size());
        const double required_depth =
            config_.per_leg_size_usdc * config_.minimum_visible_depth_ratio;
        if (visible_depth_yes < required_depth || visible_depth_no < required_depth)
        {
            return std::nullopt;
        }

        const double tick_size = market.metadata.tick_size;
        const double adjusted_yes_price = clamp_price(
            round_up_to_tick(raw_best_ask_yes + config_.slippage_buffer, tick_size));
        const double adjusted_no_price = clamp_price(
            round_up_to_tick(raw_best_ask_no + config_.slippage_buffer, tick_size));
        const double fee_multiplier = 1.0 + (config_.fee_buffer_bps / 10000.0);
        const double effective_combined =
            (adjusted_yes_price * fee_multiplier) +
            (adjusted_no_price * fee_multiplier);
        if (effective_combined >= config_.trigger_threshold)
        {
            return std::nullopt;
        }

        OpportunitySignal signal;
        signal.prepared_market = market;
        signal.signal_detected_at_ns = detected_at_ns;
        signal.yes_book_timestamp_ns = yes_book.timestamp_ns;
        signal.no_book_timestamp_ns = no_book.timestamp_ns;
        signal.snapshot_key =
            build_snapshot_key(yes_book, no_book, adjusted_yes_price, adjusted_no_price);
        signal.raw_best_ask_yes = raw_best_ask_yes;
        signal.raw_best_ask_no = raw_best_ask_no;
        signal.adjusted_yes_price = adjusted_yes_price;
        signal.adjusted_no_price = adjusted_no_price;
        signal.effective_combined = effective_combined;
        signal.visible_depth_ratio_yes = visible_depth_yes / config_.per_leg_size_usdc;
        signal.visible_depth_ratio_no = visible_depth_no / config_.per_leg_size_usdc;
        return signal;
    }

} // namespace polymarket::arb
