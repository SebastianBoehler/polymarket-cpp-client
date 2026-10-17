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
                result.price = field_number(holder, "price");
                if (!result.price)
                    throw std::invalid_argument("Gamma outcome price must be a number");
            }
            return result;
        }

        GammaEventReference event_reference(const json &item)
        {
            return {field_required_id(item, "id"), field_string(item, "slug"),
                    field_string(item, "title")};
        }

        GammaSeriesReference series_reference(const json &item)
        {
            return {field_required_id(item, "id"), field_string(item, "slug"),
                    field_string(item, "title")};
        }
    } // namespace

    std::optional<GammaMarket> parse_gamma_binary_market(const json &item)
    {
        auto labels = field_string_list(item, "outcomes");
        if (labels.size() != 2) return std::nullopt;
        const auto prices = field_sequence(item, "outcomePrices");
        const auto token_ids = field_string_list(item, "clobTokenIds");
        const auto position_ids = field_string_list(item, "positionIds");

        GammaMarket market;
        market.id = field_required_id(item, "id");
        market.version = field_string(item, "version");
        market.slug = field_string(item, "slug");
        market.condition_id = field_hex(item, "conditionId");
        market.question = field_string(item, "question");
        market.group_item_title = field_string(item, "groupItemTitle");
        market.description = field_string(item, "description");
        market.category = field_string(item, "category");
        market.image = field_string(item, "image");
        market.icon = field_string(item, "icon");

        auto &state = market.state;
        state.active = field_bool(item, "active");
        state.closed = field_bool(item, "closed");
        state.archived = field_bool(item, "archived");
        state.accepting_orders = field_bool(item, "acceptingOrders");
        state.enable_order_book = field_bool(item, "enableOrderBook");
        state.neg_risk = field_bool(item, "negRisk");
        state.combo_status = field_string(item, "comboStatus");
        state.start_date = field_string(item, "startDate");
        state.end_date = field_string(item, "endDate");
        state.closed_time = field_string(item, "closedTime");

        market.yes = outcome(std::move(labels[0]), token_ids, position_ids, prices, 0);
        market.no = outcome(std::move(labels[1]), token_ids, position_ids, prices, 1);

        auto &metrics = market.metrics;
        metrics.volume = field_number(item, "volume");
        metrics.volume_num = field_number(item, "volumeNum");
        metrics.volume_24hr = field_number(item, "volume24hr");
        metrics.volume_1wk = field_number(item, "volume1wk");
        metrics.volume_1mo = field_number(item, "volume1mo");
        metrics.volume_1yr = field_number(item, "volume1yr");
        metrics.volume_amm = field_number(item, "volumeAmm");
        metrics.volume_clob = field_number(item, "volumeClob");
        metrics.liquidity = field_number(item, "liquidity");
        metrics.liquidity_num = field_number(item, "liquidityNum");
        metrics.liquidity_clob = field_number(item, "liquidityClob");

        auto &prices_out = market.prices;
        prices_out.best_bid = field_number(item, "bestBid");
        prices_out.best_ask = field_number(item, "bestAsk");
        prices_out.last_trade_price = field_number(item, "lastTradePrice");
        prices_out.spread = field_number(item, "spread");
        prices_out.one_hour_price_change = field_number(item, "oneHourPriceChange");
        prices_out.one_day_price_change = field_number(item, "oneDayPriceChange");
        prices_out.one_week_price_change = field_number(item, "oneWeekPriceChange");
        prices_out.one_month_price_change = field_number(item, "oneMonthPriceChange");
        prices_out.one_year_price_change = field_number(item, "oneYearPriceChange");

        auto &trading = market.trading;
        trading.minimum_order_size = field_decimal_text(item, "orderMinSize");
        trading.minimum_tick_size = field_decimal_text(item, "orderPriceMinTickSize");
        trading.seconds_delay = field_integer(item, "secondsDelay");
        trading.fees_enabled = field_bool(item, "feesEnabled");
        trading.fee_type = field_string(item, "feeType");

        auto &resolution = market.resolution;
        resolution.question_id = field_string(item, "questionID");
        resolution.neg_risk_request_id = field_string(item, "negRiskRequestID");
        resolution.uma_resolution_status = field_string(item, "umaResolutionStatus");
        resolution.source = field_string(item, "resolutionSource");
        resolution.resolved_by = field_string(item, "resolvedBy");

        market.rewards.rewards_min_size = field_number(item, "rewardsMinSize");
        market.rewards.rewards_max_spread = field_number(item, "rewardsMaxSpread");
        market.rewards.holding_rewards_enabled = field_bool(item, "holdingRewardsEnabled");

        market.sports.sports_market_type = field_string(item, "sportsMarketType");
        market.sports.line = field_number(item, "line");
        market.sports.game_id = field_id(item, "gameId");
        market.sports.game_start_time = field_string(item, "gameStartTime");

        for (const auto &event : field_objects(item, "events"))
            market.events.push_back(event_reference(event));
        for (const auto &tag : field_objects(item, "tags"))
            market.tags.push_back(parse_gamma_tag_reference(tag));
        market.raw = item;
        return market;
    }

    GammaEvent parse_gamma_event(const json &item)
    {
        GammaEvent event;
        event.id = field_required_id(item, "id");
        event.parent_event_id = field_id(item, "parentEventId");
        event.ticker = field_string(item, "ticker");
        event.slug = field_string(item, "slug");
        event.title = field_string(item, "title");
        event.subtitle = field_string(item, "subtitle");
        event.description = field_string(item, "description");
        event.category = field_string(item, "category");
        event.subcategory = field_string(item, "subcategory");
        event.image = field_string(item, "image");
        event.icon = field_string(item, "icon");
        event.resolution_source = field_string(item, "resolutionSource");

        auto &state = event.state;
        state.active = field_bool(item, "active");
        state.closed = field_bool(item, "closed");
        state.archived = field_bool(item, "archived");
        state.is_new = field_bool(item, "new");
        state.featured = field_bool(item, "featured");
        state.restricted = field_bool(item, "restricted");
        state.cyom = field_bool(item, "cyom");
        state.live = field_bool(item, "live");
        state.ended = field_bool(item, "ended");
        state.comments_enabled = field_bool(item, "commentsEnabled");

        auto &schedule = event.schedule;
        schedule.start_date = field_string(item, "startDate");
        schedule.creation_date = field_string(item, "creationDate");
        schedule.end_date = field_string(item, "endDate");
        schedule.closed_time = field_string(item, "closedTime");
        schedule.start_time = field_string(item, "startTime");
        schedule.event_date = field_string(item, "eventDate");

        auto &metrics = event.metrics;
        metrics.liquidity = field_number(item, "liquidity");
        metrics.liquidity_clob = field_number(item, "liquidityClob");
        metrics.volume = field_number(item, "volume");
        metrics.volume_24hr = field_number(item, "volume24hr");
        metrics.volume_1wk = field_number(item, "volume1wk");
        metrics.volume_1mo = field_number(item, "volume1mo");
        metrics.volume_1yr = field_number(item, "volume1yr");
        metrics.open_interest = field_number(item, "openInterest");
        metrics.competitive = field_number(item, "competitive");
        metrics.comment_count = field_integer(item, "commentCount");

        auto &trading = event.trading;
        trading.enable_order_book = field_bool(item, "enableOrderBook");
        trading.neg_risk = field_bool(item, "negRisk");
        trading.neg_risk_market_id = field_string(item, "negRiskMarketID");
        trading.enable_neg_risk = field_bool(item, "enableNegRisk");
        trading.neg_risk_augmented = field_bool(item, "negRiskAugmented");
        trading.cumulative_markets = field_bool(item, "cumulativeMarkets");

        for (const auto &market : field_objects(item, "markets"))
        {
            if (auto parsed = parse_gamma_binary_market(market))
                event.markets.push_back(std::move(*parsed));
        }
        for (const auto &series : field_objects(item, "series"))
            event.series.push_back(series_reference(series));
        for (const auto &tag : field_objects(item, "tags"))
            event.tags.push_back(parse_gamma_tag_reference(tag));
        event.raw = item;
        return event;
    }
} // namespace polymarket::detail
