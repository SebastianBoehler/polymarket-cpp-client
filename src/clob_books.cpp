#include "polymarket/clob_client.hpp"
#include "clob_client_internal.hpp"
#include "order_execution.hpp"
#include "rest_orderbook_parsing.hpp"

#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    using namespace detail;

    namespace
    {
        std::optional<std::string> book_tick_size(const json &book)
        {
            const auto found = book.find("tick_size");
            if (found == book.end() || found->is_null()) return std::nullopt;
            try
            {
                auto tick_size = json_scalar_string(*found);
                (void)rounding_config_for_tick_size(tick_size);
                return tick_size;
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        std::optional<bool> book_neg_risk(const json &book)
        {
            const auto found = book.find("neg_risk");
            if (found == book.end() || !found->is_boolean()) return std::nullopt;
            return found->get<bool>();
        }
    } // namespace

    std::optional<Orderbook> ClobClient::get_order_book(const std::string &token_id)
    {
        auto result = order_book_result(token_id);
        if (!result) return std::nullopt;
        return std::move(result.value());
    }

    Result<Orderbook> ClobClient::order_book_result(const std::string &token_id)
    {
        constexpr const char *endpoint = "/book";
        if (token_id.empty())
            return Result<Orderbook>::failure({SdkErrorCode::InvalidArgument,
                                               "token_id is required", endpoint, 0, "", "", false});
        auto response = read(
            [&] { return http_.get("/book?token_id=" + percent_encode_query_value(token_id)); });
        if (!response.ok()) return Result<Orderbook>::failure(make_sdk_error(response, endpoint));

        try
        {
            const auto parsed = json::parse(response.body);
            auto book = detail::parse_rest_orderbook(parsed, token_id);
            remember_market_metadata(token_id, book_tick_size(parsed), book_neg_risk(parsed));
            return Result<Orderbook>::success(std::move(book));
        }
        catch (const std::exception &ex)
        {
            return Result<Orderbook>::failure(make_parse_error(ex.what(), endpoint, response.body));
        }
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

        auto response =
            read([&] { return http_.post("/books", book_request_body(token_ids).dump()); });
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_array() || j.size() != remaining.size())
                return {};
            struct BookMetadata
            {
                std::string asset_id;
                std::optional<std::string> tick_size;
                std::optional<bool> neg_risk;
            };
            std::vector<BookMetadata> metadata;
            metadata.reserve(j.size());
            for (const auto &item : j)
            {
                auto book = parse_orderbook(item.dump());
                if (!book || remaining.erase(book->asset_id) != 1)
                    return {};
                metadata.push_back({book->asset_id, book_tick_size(item), book_neg_risk(item)});
                result.emplace(book->asset_id, std::move(*book));
            }
            if (!remaining.empty()) return {};
            for (const auto &entry : metadata)
                remember_market_metadata(entry.asset_id, entry.tick_size, entry.neg_risk);
        }
        catch (...)
        {
            result.clear();
        }

        return result;
    }

    std::optional<PriceInfo> ClobClient::get_price(const std::string &token_id, const std::string &side)
    {
        auto response = read(
            [&]
            {
                return http_.get("/price?token_id=" + percent_encode_query_value(token_id) +
                                 "&side=" + percent_encode_query_value(side));
            });
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
        auto response = read(
            [&]
            {
                return http_.post("/prices", book_request_body(token_ids, normalized_side).dump());
            });
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
        auto response = read(
            [&]
            {
                return http_.get("/last-trade-price?token_id=" +
                                 percent_encode_query_value(token_id));
            });
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

        auto response = read(
            [&] { return http_.post("/last-trades-prices", book_request_body(token_ids).dump()); });
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
        auto response = read(
            [&]
            { return http_.get("/midpoint?token_id=" + percent_encode_query_value(token_id)); });
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

        auto response =
            read([&] { return http_.post("/midpoints", book_request_body(token_ids).dump()); });
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
        auto response = read(
            [&] { return http_.get("/spread?token_id=" + percent_encode_query_value(token_id)); });
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

        auto response =
            read([&] { return http_.post("/spreads", book_request_body(token_ids).dump()); });
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
