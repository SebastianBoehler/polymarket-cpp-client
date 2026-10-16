#include "gamma_parsing.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

// Field names follow py-sdk models/gamma/market.py and event.py.
namespace polymarket::detail
{
    namespace
    {
        GammaOutcome outcome(std::string label, const std::vector<std::string> &token_ids,
                             const std::vector<std::string> &position_ids,
                             const std::vector<json> &prices, std::size_t index)
        {
            GammaOutcome result;
            result.label = std::move(label);
            if (index < token_ids.size()) result.token_id = token_ids[index];
            if (index < position_ids.size()) result.position_id = position_ids[index];
            if (index < prices.size())
            {
                const json holder{{"price", prices[index]}};
                result.price = gamma_number(holder, "price");
                if (!result.price)
                    throw std::invalid_argument("Gamma outcome price must be a number");
            }
            return result;
        }

        GammaEventReference event_reference(const json &item)
        {
            return {gamma_required_id(item, "id"), gamma_string(item, "slug"),
                    gamma_string(item, "title")};
        }

        GammaSeriesReference series_reference(const json &item)
        {
            return {gamma_required_id(item, "id"), gamma_string(item, "slug"),
                    gamma_string(item, "title")};
        }
    } // namespace

    std::optional<GammaMarket> parse_gamma_binary_market(const json &item)
    {
        auto labels = gamma_string_list(item, "outcomes");
        if (labels.size() != 2) return std::nullopt;
        const auto prices = gamma_sequence(item, "outcomePrices");
        const auto token_ids = gamma_string_list(item, "clobTokenIds");
        const auto position_ids = gamma_string_list(item, "positionIds");

        GammaMarket market;
        market.id = gamma_required_id(item, "id");
        market.version = gamma_string(item, "version");
        market.slug = gamma_string(item, "slug");
        market.condition_id = gamma_hex(item, "conditionId");
        market.question = gamma_string(item, "question");
        market.group_item_title = gamma_string(item, "groupItemTitle");
        market.description = gamma_string(item, "description");
        market.category = gamma_string(item, "category");
        market.image = gamma_string(item, "image");
        market.icon = gamma_string(item, "icon");

        auto &state = market.state;
        state.active = gamma_bool(item, "active");
        state.closed = gamma_bool(item, "closed");
        state.archived = gamma_bool(item, "archived");
        state.accepting_orders = gamma_bool(item, "acceptingOrders");
        state.enable_order_book = gamma_bool(item, "enableOrderBook");
        state.neg_risk = gamma_bool(item, "negRisk");
        state.combo_status = gamma_string(item, "comboStatus");
        state.start_date = gamma_string(item, "startDate");
        state.end_date = gamma_string(item, "endDate");
        state.closed_time = gamma_string(item, "closedTime");

        market.yes = outcome(std::move(labels[0]), token_ids, position_ids, prices, 0);
        market.no = outcome(std::move(labels[1]), token_ids, position_ids, prices, 1);

        auto &metrics = market.metrics;
        metrics.volume = gamma_number(item, "volume");
        metrics.volume_num = gamma_number(item, "volumeNum");
        metrics.volume_24hr = gamma_number(item, "volume24hr");
        metrics.volume_1wk = gamma_number(item, "volume1wk");
        metrics.volume_1mo = gamma_number(item, "volume1mo");
        metrics.volume_1yr = gamma_number(item, "volume1yr");
        metrics.volume_amm = gamma_number(item, "volumeAmm");
        metrics.volume_clob = gamma_number(item, "volumeClob");
        metrics.liquidity = gamma_number(item, "liquidity");
        metrics.liquidity_num = gamma_number(item, "liquidityNum");
        metrics.liquidity_clob = gamma_number(item, "liquidityClob");

        auto &prices_out = market.prices;
        prices_out.best_bid = gamma_number(item, "bestBid");
        prices_out.best_ask = gamma_number(item, "bestAsk");
        prices_out.last_trade_price = gamma_number(item, "lastTradePrice");
        prices_out.spread = gamma_number(item, "spread");
        prices_out.one_hour_price_change = gamma_number(item, "oneHourPriceChange");
        prices_out.one_day_price_change = gamma_number(item, "oneDayPriceChange");
        prices_out.one_week_price_change = gamma_number(item, "oneWeekPriceChange");
        prices_out.one_month_price_change = gamma_number(item, "oneMonthPriceChange");
        prices_out.one_year_price_change = gamma_number(item, "oneYearPriceChange");

        auto &trading = market.trading;
        trading.minimum_order_size = gamma_decimal_text(item, "orderMinSize");
        trading.minimum_tick_size = gamma_decimal_text(item, "orderPriceMinTickSize");
        trading.seconds_delay = gamma_integer(item, "secondsDelay");
        trading.fees_enabled = gamma_bool(item, "feesEnabled");
        trading.fee_type = gamma_string(item, "feeType");

        auto &resolution = market.resolution;
        resolution.question_id = gamma_string(item, "questionID");
        resolution.neg_risk_request_id = gamma_string(item, "negRiskRequestID");
        resolution.uma_resolution_status = gamma_string(item, "umaResolutionStatus");
        resolution.source = gamma_string(item, "resolutionSource");
        resolution.resolved_by = gamma_string(item, "resolvedBy");

        market.rewards.rewards_min_size = gamma_number(item, "rewardsMinSize");
        market.rewards.rewards_max_spread = gamma_number(item, "rewardsMaxSpread");
        market.rewards.holding_rewards_enabled = gamma_bool(item, "holdingRewardsEnabled");

        market.sports.sports_market_type = gamma_string(item, "sportsMarketType");
        market.sports.line = gamma_number(item, "line");
        market.sports.game_id = gamma_id(item, "gameId");
        market.sports.game_start_time = gamma_string(item, "gameStartTime");

        for (const auto &event : gamma_objects(item, "events"))
            market.events.push_back(event_reference(event));
        for (const auto &tag : gamma_objects(item, "tags"))
            market.tags.push_back(parse_gamma_tag_reference(tag));
        market.raw = item;
        return market;
    }

    GammaEvent parse_gamma_event(const json &item)
    {
        GammaEvent event;
        event.id = gamma_required_id(item, "id");
        event.parent_event_id = gamma_id(item, "parentEventId");
        event.ticker = gamma_string(item, "ticker");
        event.slug = gamma_string(item, "slug");
        event.title = gamma_string(item, "title");
        event.subtitle = gamma_string(item, "subtitle");
        event.description = gamma_string(item, "description");
        event.category = gamma_string(item, "category");
        event.subcategory = gamma_string(item, "subcategory");
        event.image = gamma_string(item, "image");
        event.icon = gamma_string(item, "icon");
        event.resolution_source = gamma_string(item, "resolutionSource");

        auto &state = event.state;
        state.active = gamma_bool(item, "active");
        state.closed = gamma_bool(item, "closed");
        state.archived = gamma_bool(item, "archived");
        state.is_new = gamma_bool(item, "new");
        state.featured = gamma_bool(item, "featured");
        state.restricted = gamma_bool(item, "restricted");
        state.cyom = gamma_bool(item, "cyom");
        state.live = gamma_bool(item, "live");
        state.ended = gamma_bool(item, "ended");
        state.comments_enabled = gamma_bool(item, "commentsEnabled");

        auto &schedule = event.schedule;
        schedule.start_date = gamma_string(item, "startDate");
        schedule.creation_date = gamma_string(item, "creationDate");
        schedule.end_date = gamma_string(item, "endDate");
        schedule.closed_time = gamma_string(item, "closedTime");
        schedule.start_time = gamma_string(item, "startTime");
        schedule.event_date = gamma_string(item, "eventDate");

        auto &metrics = event.metrics;
        metrics.liquidity = gamma_number(item, "liquidity");
        metrics.liquidity_clob = gamma_number(item, "liquidityClob");
        metrics.volume = gamma_number(item, "volume");
        metrics.volume_24hr = gamma_number(item, "volume24hr");
        metrics.volume_1wk = gamma_number(item, "volume1wk");
        metrics.volume_1mo = gamma_number(item, "volume1mo");
        metrics.volume_1yr = gamma_number(item, "volume1yr");
        metrics.open_interest = gamma_number(item, "openInterest");
        metrics.competitive = gamma_number(item, "competitive");
        metrics.comment_count = gamma_integer(item, "commentCount");

        auto &trading = event.trading;
        trading.enable_order_book = gamma_bool(item, "enableOrderBook");
        trading.neg_risk = gamma_bool(item, "negRisk");
        trading.neg_risk_market_id = gamma_string(item, "negRiskMarketID");
        trading.enable_neg_risk = gamma_bool(item, "enableNegRisk");
        trading.neg_risk_augmented = gamma_bool(item, "negRiskAugmented");
        trading.cumulative_markets = gamma_bool(item, "cumulativeMarkets");

        for (const auto &market : gamma_objects(item, "markets"))
        {
            if (auto parsed = parse_gamma_binary_market(market))
                event.markets.push_back(std::move(*parsed));
        }
        for (const auto &series : gamma_objects(item, "series"))
            event.series.push_back(series_reference(series));
        for (const auto &tag : gamma_objects(item, "tags"))
            event.tags.push_back(parse_gamma_tag_reference(tag));
        event.raw = item;
        return event;
    }
} // namespace polymarket::detail
