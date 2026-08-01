#include "clob_client.hpp"
#include "clob_client_internal.hpp"

#include <nlohmann/json.hpp>
#include <limits>
#include <set>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    using namespace detail;

    std::optional<OrderScoringResult> ClobClient::is_order_scoring(const std::string &order_id)
    {
        constexpr const char *endpoint = "/order-scoring";
        auto headers = get_l2_headers("GET", endpoint, "");
        auto response = http_.get(std::string(endpoint) +
                                      "?order_id=" + percent_encode_query_value(order_id),
                                  headers);

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_object() || !j.contains("scoring") ||
                !j["scoring"].is_boolean())
                return std::nullopt;
            OrderScoringResult result;
            result.scoring = j["scoring"].get<bool>();
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<OrderScoringResult> ClobClient::is_order_scoring(const SignedOrder &)
    {
        throw std::invalid_argument(
            "SignedOrder has no server order ID; call is_order_scoring(order_id)");
    }

    std::vector<OrderScoringResult> ClobClient::are_orders_scoring(const std::vector<std::string> &order_ids)
    {
        std::vector<OrderScoringResult> results;
        if (order_ids.empty())
            return results;
        const json body = order_ids;
        std::string body_str = body.dump();
        auto headers = get_l2_headers("POST", "/orders-scoring", body_str);
        auto response = http_.post("/orders-scoring", body_str, headers);

        if (!response.ok())
            return results;

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_object())
                return {};
            const std::set<std::string> requested(order_ids.begin(), order_ids.end());
            if (j.size() != requested.size())
                return {};
            for (const auto &[order_id, value] : j.items())
            {
                if (!requested.contains(order_id) || !value.is_boolean())
                    return {};
            }
            for (const auto &order_id : order_ids)
                results.push_back({j.at(order_id).get<bool>()});
        }
        catch (...)
        {
            return {};
        }

        return results;
    }

    std::vector<OrderScoringResult> ClobClient::are_orders_scoring(const std::vector<SignedOrder> &)
    {
        throw std::invalid_argument(
            "SignedOrder has no server order ID; call are_orders_scoring(order_ids)");
    }

    std::vector<ClobClient::Notification> ClobClient::get_notifications()
    {
        std::vector<Notification> result;

        auto headers = get_l2_headers("GET", "/notifications", "");
        auto response = http_.get("/notifications?signature_type=" +
                                      percent_encode_query_value(std::to_string(static_cast<int>(sig_type_))),
                                  headers);

        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_array())
                return {};
            for (const auto &item : j)
            {
                if (!item.is_object() || !item.contains("type") ||
                    !item.contains("owner") || !item["owner"].is_string() ||
                    !item.contains("payload") || !item["payload"].is_object())
                    return {};
                const auto &type = item["type"];
                uint64_t raw_type = 0;
                if (type.is_number_unsigned())
                    raw_type = type.get<uint64_t>();
                else if (type.is_number_integer())
                {
                    const auto signed_type = type.get<int64_t>();
                    if (signed_type < 0) return {};
                    raw_type = static_cast<uint64_t>(signed_type);
                }
                else
                    return {};
                if (raw_type > std::numeric_limits<uint32_t>::max()) return {};
                result.push_back({static_cast<uint32_t>(raw_type),
                                  item["owner"].get<std::string>(), item["payload"]});
            }
        }
        catch (...)
        {
            return {};
        }

        return result;
    }

    bool ClobClient::drop_notifications(const std::vector<std::string> &notification_ids)
    {
        std::string path = "/notifications?ids=";
        for (size_t i = 0; i < notification_ids.size(); ++i)
        {
            if (i > 0)
                path += ",";
            path += percent_encode_query_value(notification_ids[i]);
        }
        auto headers = get_l2_headers("DELETE", "/notifications", "");
        auto response = http_.del(path, "", headers);
        return response.ok();
    }

    std::optional<ClobClient::EarningsInfo> ClobClient::get_total_earnings_for_user_for_day(const std::string &date)
    {
        const auto earnings = get_total_earnings_for_user_for_day_all(date);
        if (earnings.empty())
            return std::nullopt;
        return earnings.front();
    }

    std::vector<ClobClient::EarningsInfo> ClobClient::get_total_earnings_for_user_for_day_all(const std::string &date)
    {
        constexpr const char *endpoint = "/rewards/user/total";
        auto headers = get_l2_headers("GET", endpoint, "");
        const std::string path = std::string(endpoint) + "?date=" + percent_encode_query_value(date) +
                                 "&signature_type=" + std::to_string(static_cast<int>(sig_type_));
        auto response = http_.get(path, headers);
        if (!response.ok())
            return {};
        std::vector<EarningsInfo> result;
        try
        {
            const auto items = json::parse(response.body);
            if (!items.is_array())
                return {};
            std::vector<EarningsInfo> parsed;
            parsed.reserve(items.size());
            for (const auto &item : items)
                parsed.push_back(parse_total_user_earning_info(item));
            result = std::move(parsed);
        }
        catch (...)
        {
            return {};
        }
        return result;
    }

    std::optional<ClobClient::FeeRateInfo> ClobClient::get_fee_rate(const std::string &token_id)
    {
        if (token_id.empty())
            return std::nullopt;
        const std::string path = "/fee-rate?token_id=" + percent_encode_query_value(token_id);
        auto response = http_.get(path);

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            FeeRateInfo info;
            info.base_fee = j.contains("base_fee") ? json_scalar_string(j["base_fee"], "0") : "0";
            info.maker = j.value("maker", "0");
            info.taker = j.value("taker", "0");
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    // ============================================================
    // POSITION MANAGEMENT (Data API)
    // ============================================================
}
