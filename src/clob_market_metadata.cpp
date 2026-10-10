#include "polymarket/clob_client.hpp"
#include "clob_client_internal.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    using namespace detail;

    namespace
    {
        // Tick sizes change as prices approach 0 or 1, so they expire. A
        // token's neg-risk flag is fixed at market creation; caching it for
        // the client's lifetime keeps an extra round trip off the order path.
        // clear_market_metadata_cache() still evicts both.
        constexpr auto METADATA_CACHE_TTL = std::chrono::minutes(5);

        class MetadataFetchCompletion
        {
        public:
            MetadataFetchCompletion(std::mutex &mutex,
                                    std::condition_variable &condition,
                                    std::set<std::string> &in_flight,
                                    std::string key)
                : mutex_(mutex), condition_(condition), in_flight_(in_flight), key_(std::move(key))
            {
            }

            ~MetadataFetchCompletion()
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    in_flight_.erase(key_);
                }
                condition_.notify_all();
            }

        private:
            std::mutex &mutex_;
            std::condition_variable &condition_;
            std::set<std::string> &in_flight_;
            std::string key_;
        };
    }

    std::optional<TickSizeInfo> ClobClient::get_tick_size(const std::string &token_id)
    {
        auto result = tick_size_result(token_id);
        if (!result) return std::nullopt;
        return std::move(result.value());
    }

    Result<TickSizeInfo> ClobClient::tick_size_result(const std::string &token_id)
    {
        constexpr const char *endpoint = "/tick-size";
        const std::string in_flight_key = "tick:" + token_id;
        {
            std::unique_lock<std::mutex> lock(metadata_cache_mutex_);
            while (true)
            {
                const auto cached = tick_size_cache_.find(token_id);
                if (cached != tick_size_cache_.end())
                {
                    if (std::chrono::steady_clock::now() < cached->second.expires_at)
                        return Result<TickSizeInfo>::success(cached->second.value);
                    tick_size_cache_.erase(cached);
                }
                if (metadata_cache_in_flight_.insert(in_flight_key).second)
                    break;
                metadata_cache_cv_.wait(lock, [&]
                                        { return metadata_cache_in_flight_.count(in_flight_key) == 0; });
            }
        }
        MetadataFetchCompletion completion(metadata_cache_mutex_, metadata_cache_cv_,
                                           metadata_cache_in_flight_, in_flight_key);

        auto response = read(
            [&]
            { return http_.get("/tick-size?token_id=" + percent_encode_query_value(token_id)); });
        if (!response.ok())
            return Result<TickSizeInfo>::failure(make_sdk_error(response, endpoint));

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_object() || !j.contains("minimum_tick_size") || j["minimum_tick_size"].is_null())
                return Result<TickSizeInfo>::failure(
                    make_parse_error("missing minimum_tick_size", endpoint, response.body));

            TickSizeInfo info;
            info.minimum_tick_size = json_scalar_string(j["minimum_tick_size"]);
            (void)json_orderbook_price(j["minimum_tick_size"]);
            std::lock_guard<std::mutex> lock(metadata_cache_mutex_);
            return Result<TickSizeInfo>::success(
                tick_size_cache_
                    .insert_or_assign(
                        token_id,
                        MetadataCacheEntry<TickSizeInfo>{
                            std::move(info), std::chrono::steady_clock::now() + METADATA_CACHE_TTL})
                    .first->second.value);
        }
        catch (const std::exception &ex)
        {
            return Result<TickSizeInfo>::failure(
                make_parse_error(ex.what(), endpoint, response.body));
        }
    }

    std::optional<NegRiskInfo> ClobClient::get_neg_risk(const std::string &token_id)
    {
        auto result = neg_risk_result(token_id);
        if (!result) return std::nullopt;
        return result.value();
    }

    Result<NegRiskInfo> ClobClient::neg_risk_result(const std::string &token_id)
    {
        constexpr const char *endpoint = "/neg-risk";
        const std::string in_flight_key = "neg-risk:" + token_id;
        {
            std::unique_lock<std::mutex> lock(metadata_cache_mutex_);
            while (true)
            {
                const auto cached = neg_risk_cache_.find(token_id);
                if (cached != neg_risk_cache_.end())
                {
                    if (std::chrono::steady_clock::now() < cached->second.expires_at)
                        return Result<NegRiskInfo>::success(cached->second.value);
                    neg_risk_cache_.erase(cached);
                }
                if (metadata_cache_in_flight_.insert(in_flight_key).second)
                    break;
                metadata_cache_cv_.wait(lock, [&]
                                        { return metadata_cache_in_flight_.count(in_flight_key) == 0; });
            }
        }
        MetadataFetchCompletion completion(metadata_cache_mutex_, metadata_cache_cv_,
                                           metadata_cache_in_flight_, in_flight_key);

        auto response = read(
            [&]
            { return http_.get("/neg-risk?token_id=" + percent_encode_query_value(token_id)); });
        if (!response.ok()) return Result<NegRiskInfo>::failure(make_sdk_error(response, endpoint));

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_object() || !j.contains("neg_risk") || !j["neg_risk"].is_boolean())
                return Result<NegRiskInfo>::failure(
                    make_parse_error("missing boolean neg_risk", endpoint, response.body));

            NegRiskInfo info;
            info.neg_risk = j["neg_risk"].get<bool>();
            std::lock_guard<std::mutex> lock(metadata_cache_mutex_);
            return Result<NegRiskInfo>::success(
                neg_risk_cache_
                    .insert_or_assign(token_id,
                                      MetadataCacheEntry<NegRiskInfo>{
                                          info, std::chrono::steady_clock::time_point::max()})
                    .first->second.value);
        }
        catch (const std::exception &ex)
        {
            return Result<NegRiskInfo>::failure(
                make_parse_error(ex.what(), endpoint, response.body));
        }
    }

    void ClobClient::clear_market_metadata_cache(const std::string &token_id)
    {
        std::unique_lock<std::mutex> lock(metadata_cache_mutex_);
        if (token_id.empty())
        {
            metadata_cache_cv_.wait(lock, [&]
                                    { return metadata_cache_in_flight_.empty(); });
            tick_size_cache_.clear();
            neg_risk_cache_.clear();
            return;
        }

        const std::string tick_key = "tick:" + token_id;
        const std::string neg_risk_key = "neg-risk:" + token_id;
        metadata_cache_cv_.wait(lock, [&]
                                {
                                    return metadata_cache_in_flight_.count(tick_key) == 0 &&
                                           metadata_cache_in_flight_.count(neg_risk_key) == 0;
                                });
        tick_size_cache_.erase(token_id);
        neg_risk_cache_.erase(token_id);
    }

    void ClobClient::evict_tick_size(const std::string &token_id)
    {
        std::lock_guard<std::mutex> lock(metadata_cache_mutex_);
        tick_size_cache_.erase(token_id);
    }

    std::vector<ClobClient::PriceHistoryPoint> ClobClient::get_prices_history(
        const std::string &token_id,
        uint64_t start_ts,
        uint64_t end_ts,
        const std::string &interval,
        const std::string &fidelity)
    {
        std::vector<PriceHistoryPoint> result;
        if ((start_ts == 0) != (end_ts == 0))
        {
            throw std::invalid_argument("price history requires both start_ts and end_ts");
        }

        std::string path = "/prices-history?market=" + percent_encode_query_value(token_id);
        if (start_ts > 0)
        {
            path += "&startTs=" + std::to_string(start_ts);
            path += "&endTs=" + std::to_string(end_ts);
        }
        else if (!interval.empty())
        {
            path += "&interval=" + percent_encode_query_value(interval);
        }
        if (!fidelity.empty())
        {
            path += "&fidelity=" + percent_encode_query_value(fidelity);
        }

        auto response = read([&] { return http_.get(path); });
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.contains("history") && j["history"].is_array())
            {
                for (const auto &item : j["history"])
                {
                    if (!item.is_object() || !item.contains("t"))
                        throw std::invalid_argument(
                            "price history point requires a timestamp");
                    const auto &timestamp = item["t"];
                    PriceHistoryPoint point;
                    if (timestamp.is_number_unsigned())
                        point.timestamp = timestamp.get<uint64_t>();
                    else if (timestamp.is_number_integer())
                    {
                        const auto signed_timestamp = timestamp.get<int64_t>();
                        if (signed_timestamp <= 0)
                            throw std::invalid_argument(
                                "price history timestamp must be positive");
                        point.timestamp = static_cast<uint64_t>(signed_timestamp);
                    }
                    else
                        throw std::invalid_argument(
                            "price history timestamp must be an integer");
                    if (point.timestamp == 0)
                        throw std::invalid_argument(
                            "price history timestamp must be positive");
                    point.price = json_probability(item.at("p"));
                    result.push_back(point);
                }
            }
        }
        catch (...)
        {
            result.clear();
        }

        return result;
    }

    std::optional<ClobClient::LiveActivityMarket> ClobClient::get_market_live_activity(
        const std::string &condition_id)
    {
        auto response = read([&] { return http_.get("/markets/live-activity/" + condition_id); });
        if (!response.ok())
            return std::nullopt;

        try
        {
            const auto item = json::parse(response.body);
            if (!item.is_object()) return std::nullopt;
            const auto required_string = [&item](const char *field)
            {
                const auto &value = item.at(field);
                if (!value.is_string())
                    throw std::invalid_argument(
                        std::string("live activity ") + field +
                        " must be a string");
                return value.get<std::string>();
            };
            LiveActivityMarket market;
            market.condition_id = required_string("condition_id");
            if (market.condition_id.empty() ||
                market.condition_id != condition_id)
                return std::nullopt;
            const auto &id = item.at("id");
            if (!id.is_number_integer() && !id.is_number_unsigned())
                return std::nullopt;
            market.id = id.get<int64_t>();
            market.question = required_string("question");
            market.market_slug = required_string("market_slug");
            market.event_slug = required_string("event_slug");
            market.series_slug = required_string("series_slug");
            market.icon = required_string("icon");
            market.image = required_string("image");
            const auto &tags = item.at("tags");
            if (!tags.is_array()) return std::nullopt;
            market.tags = tags.get<std::vector<std::string>>();
            return market;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<Trade> ClobClient::get_market_trades_events(const std::string &,
                                                            const std::string &)
    {
        throw std::logic_error(
            "get_market_trades_events is obsolete; call get_market_live_activity(condition_id)");
    }

    // ============================================================
    // AUTHENTICATED ENDPOINTS (L1)
    // ============================================================
}
