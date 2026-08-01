#include "clob_client.hpp"
#include "clob_client_internal.hpp"

#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    using namespace detail;

    std::optional<Orderbook> ClobClient::get_order_book(const std::string &token_id)
    {
        if (token_id.empty()) return std::nullopt;
        auto response = http_.get("/book?token_id=" + percent_encode_query_value(token_id));
        if (!response.ok())
            return std::nullopt;

        return parse_orderbook(response.body, token_id);
    }

    std::map<std::string, Orderbook> ClobClient::get_order_books(const std::vector<std::string> &token_ids)
    {
        std::map<std::string, Orderbook> result;
        if (token_ids.empty())
        {
            return result;
        }
        std::set<std::string> remaining(token_ids.begin(), token_ids.end());
        if (remaining.size() != token_ids.size() || remaining.count("") != 0)
            return result;

        auto response = http_.post("/books", book_request_body(token_ids).dump());
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_array() || j.size() != remaining.size())
                return {};
            for (const auto &item : j)
            {
                auto book = parse_orderbook(item.dump());
                if (!book || remaining.erase(book->asset_id) != 1)
                    return {};
                result.emplace(book->asset_id, std::move(*book));
            }
            if (!remaining.empty()) return {};
        }
        catch (...)
        {
            result.clear();
        }

        return result;
    }

    std::optional<PriceInfo> ClobClient::get_price(const std::string &token_id, const std::string &side)
    {
        auto response = http_.get("/price?token_id=" + percent_encode_query_value(token_id) +
                                  "&side=" + percent_encode_query_value(side));
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            PriceInfo info;
            info.token_id = token_id;
            info.price = json_probability(j.at("price"));
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<PriceInfo> ClobClient::get_prices(const std::vector<std::string> &token_ids, const std::string &side)
    {
        std::vector<PriceInfo> result;
        if (token_ids.empty())
        {
            return result;
        }

        const auto normalized_side = uppercase(side);
        auto response = http_.post("/prices", book_request_body(token_ids, normalized_side).dump());
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_object())
            {
                for (const auto &token_id : token_ids)
                {
                    if (!j.contains(token_id) || !j[token_id].contains(normalized_side))
                        continue;
                    PriceInfo info;
                    info.token_id = token_id;
                    info.price = json_probability(j[token_id][normalized_side]);
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
            result.clear();
        }

        return result;
    }

    std::optional<PriceInfo> ClobClient::get_last_trade_price(const std::string &token_id)
    {
        auto response = http_.get("/last-trade-price?token_id=" + percent_encode_query_value(token_id));
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            PriceInfo info;
            info.token_id = token_id;
            info.price = json_probability(j.at("price"));
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<PriceInfo> ClobClient::get_last_trades_prices(const std::vector<std::string> &token_ids)
    {
        std::vector<PriceInfo> result;
        if (token_ids.empty())
        {
            return result;
        }

        auto response = http_.post("/last-trades-prices", book_request_body(token_ids).dump());
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
                {
                    PriceInfo info;
                    info.token_id = item.at("token_id").get<std::string>();
                    info.price = json_probability(item.at("price"));
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
            result.clear();
        }

        return result;
    }

    std::optional<MidpointInfo> ClobClient::get_midpoint(const std::string &token_id)
    {
        auto response = http_.get("/midpoint?token_id=" + percent_encode_query_value(token_id));
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            MidpointInfo info;
            info.token_id = token_id;
            info.mid = json_probability(j.at("mid"));
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<MidpointInfo> ClobClient::get_midpoints(const std::vector<std::string> &token_ids)
    {
        std::vector<MidpointInfo> result;
        if (token_ids.empty())
        {
            return result;
        }

        auto response = http_.post("/midpoints", book_request_body(token_ids).dump());
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_object())
            {
                for (const auto &token_id : token_ids)
                {
                    if (!j.contains(token_id))
                        continue;
                    MidpointInfo info;
                    info.token_id = token_id;
                    info.mid = json_probability(j[token_id]);
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
            result.clear();
        }

        return result;
    }

    std::optional<SpreadInfo> ClobClient::get_spread(const std::string &token_id)
    {
        auto response = http_.get("/spread?token_id=" + percent_encode_query_value(token_id));
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            SpreadInfo info;
            info.token_id = token_id;
            info.spread = json_probability(j.at("spread"));
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<SpreadInfo> ClobClient::get_spreads(const std::vector<std::string> &token_ids)
    {
        std::vector<SpreadInfo> result;
        if (token_ids.empty())
        {
            return result;
        }

        auto response = http_.post("/spreads", book_request_body(token_ids).dump());
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_object())
            {
                for (const auto &token_id : token_ids)
                {
                    if (!j.contains(token_id))
                        continue;
                    SpreadInfo info;
                    info.token_id = token_id;
                    info.spread = json_probability(j[token_id]);
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
            result.clear();
        }

        return result;
    }
}
