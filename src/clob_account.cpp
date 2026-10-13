#include "polymarket/clob_client.hpp"
#include "clob_client_internal.hpp"
#include "polymarket/pagination.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    namespace
    {
        std::string balance_value_string(const json &value)
        {
            if (value.is_string())
                return value.get<std::string>();
            if (value.is_number())
                return value.dump();
            throw std::invalid_argument(
                "balance and allowance values must be strings or numbers");
        }
    }

    using namespace detail;

    ApiCredentials ClobClient::create_api_key(uint64_t nonce)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }
        auto credentials = order_signer_->create_api_credentials(http_, nonce);
        api_creds_ = std::make_unique<ApiCredentials>(credentials);
        return credentials;
    }

    ApiCredentials ClobClient::derive_api_key()
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }
        auto credentials = order_signer_->derive_api_credentials(http_);
        api_creds_ = std::make_unique<ApiCredentials>(credentials);
        return credentials;
    }

    ApiCredentials ClobClient::create_or_derive_api_key()
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }
        auto credentials = order_signer_->create_or_derive_api_credentials(http_);
        api_creds_ = std::make_unique<ApiCredentials>(credentials);
        return credentials;
    }

    std::vector<std::string> ClobClient::get_api_keys()
    {
        std::vector<std::string> result;

        auto response = read(
            [&]
            { return http_.get("/auth/api-keys", get_l2_headers("GET", "/auth/api-keys", "")); });

        if (!response.ok())
            return result;

        try
        {
            const auto parsed = json::parse(response.body);
            if (parsed.is_object() && parsed.contains("apiKeys") &&
                parsed["apiKeys"].is_array())
            {
                for (const auto &item : parsed["apiKeys"])
                {
                    result.push_back(item.get<std::string>());
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    bool ClobClient::delete_api_key()
    {
        auto headers = get_l2_headers("DELETE", "/auth/api-key", "");
        return http_.del("/auth/api-key", "", headers).ok();
    }

    std::optional<OpenOrder> ClobClient::get_order(const std::string &order_id)
    {
        auto result = get_order_result(order_id);
        return result ? result.value() : std::nullopt;
    }

    Result<std::optional<OpenOrder>> ClobClient::get_order_result(const std::string &order_id)
    {
        const std::string endpoint = "/data/order/" + order_id;
        if (!order_signer_ || !api_creds_)
        {
            return Result<std::optional<OpenOrder>>::failure(make_auth_error("Client not authenticated", endpoint));
        }

        auto response =
            read([&] { return http_.get(endpoint, get_l2_headers("GET", endpoint, "")); });

        if (!response.ok())
        {
            return Result<std::optional<OpenOrder>>::failure(make_sdk_error(response, endpoint));
        }

        try
        {
            return Result<std::optional<OpenOrder>>::success(
                detail::parse_open_order_json(response.body));
        }
        catch (const std::exception &ex)
        {
            return Result<std::optional<OpenOrder>>::failure(
                make_parse_error(ex.what(), endpoint, response.body));
        }
    }

    std::vector<OpenOrder> ClobClient::get_open_orders(const std::string &market)
    {
        auto result = get_open_orders_result(market);
        return result ? result.value() : std::vector<OpenOrder>{};
    }

    Result<std::vector<OpenOrder>> ClobClient::get_open_orders_result(const std::string &market)
    {
        return open_orders_pager(market).collect();
    }

    Paginator<OpenOrder> ClobClient::open_orders_pager(const std::string &market,
                                                       const std::string &start_cursor)
    {
        constexpr const char *endpoint = "/data/orders";
        std::string prefix = endpoint;
        prefix += market.empty() ? "?" : "?market=" + percent_encode_query_value(market) + "&";
        prefix += "next_cursor=";
        return Paginator<OpenOrder>::cursor(
            endpoint,
            [this,
             prefix = std::move(prefix)](const PageRequest &request) -> Result<Page<OpenOrder>>
            {
                if (!order_signer_ || !api_creds_)
                {
                    return Result<Page<OpenOrder>>::failure(
                        make_auth_error("Client not authenticated", endpoint));
                }

                const std::string path = prefix + percent_encode_query_value(request.cursor);
                auto response =
                    read([&] { return http_.get(path, get_l2_headers("GET", endpoint, "")); });

                if (!response.ok())
                {
                    return Result<Page<OpenOrder>>::failure(make_sdk_error(response, endpoint));
                }

                try
                {
                    auto page = detail::parse_open_order_page_json(response.body);
                    return Result<Page<OpenOrder>>::success(
                        Page<OpenOrder>{std::move(page.orders), std::move(page.next_cursor)});
                }
                catch (const std::exception &ex)
                {
                    return Result<Page<OpenOrder>>::failure(
                        make_parse_error(ex.what(), endpoint, response.body));
                }
            },
            start_cursor.empty() ? INITIAL_CURSOR : start_cursor, END_CURSOR);
    }

    std::vector<Trade> ClobClient::get_trades(const std::string &next_cursor)
    {
        if (!order_signer_ || !api_creds_) throw std::runtime_error("Client not authenticated");
        auto trades = trades_pager(next_cursor).collect();
        return trades ? std::move(trades.value()) : std::vector<Trade>{};
    }

    Paginator<Trade> ClobClient::trades_pager(const std::string &start_cursor)
    {
        constexpr const char *endpoint = "/data/trades";
        return Paginator<Trade>::cursor(
            endpoint,
            [this](const PageRequest &request) -> Result<Page<Trade>>
            {
                if (!order_signer_ || !api_creds_)
                    return Result<Page<Trade>>::failure(
                        make_auth_error("Client not authenticated", endpoint));

                const std::string path = std::string(endpoint) + "?next_cursor=" +
                                         percent_encode_query_value(request.cursor);
                auto response =
                    read([&] { return http_.get(path, get_l2_headers("GET", endpoint, "")); });

                if (!response.ok())
                    return Result<Page<Trade>>::failure(make_sdk_error(response, endpoint));

                try
                {
                    auto page = detail::parse_trade_page_json(response.body);
                    return Result<Page<Trade>>::success(
                        Page<Trade>{std::move(page.trades), std::move(page.next_cursor)});
                }
                catch (const std::exception &ex)
                {
                    return Result<Page<Trade>>::failure(
                        make_parse_error(ex.what(), endpoint, response.body));
                }
            },
            start_cursor.empty() ? INITIAL_CURSOR : start_cursor, END_CURSOR);
    }

    std::optional<BalanceAllowance> ClobClient::get_balance_allowance(
        const std::string &asset_type, const std::string &token_id)
    {
        std::string request_path = "/balance-allowance";
        std::string path = request_path + "?asset_type=" + percent_encode_query_value(asset_type) +
                           "&signature_type=" + std::to_string(static_cast<int>(sig_type_));
        if (!token_id.empty())
            path += "&token_id=" + percent_encode_query_value(token_id);
        auto response =
            read([&] { return http_.get(path, get_l2_headers("GET", request_path, "")); });

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            if (!j.is_object() || !j.contains("balance") ||
                !j.contains("allowances") || !j["allowances"].is_object())
            {
                throw std::invalid_argument(
                    "balance response requires balance and allowances");
            }
            BalanceAllowance ba;
            ba.balance = balance_value_string(j["balance"]);
            for (auto allowance = j["allowances"].begin();
                 allowance != j["allowances"].end(); ++allowance)
            {
                ba.allowances.emplace(allowance.key(),
                                      balance_value_string(allowance.value()));
            }
            return ba;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ClobClient::update_balance_allowance(const std::string &asset_type,
                                              const std::string &token_id)
    {
        std::string request_path = "/balance-allowance/update";
        std::string path = request_path + "?asset_type=" + percent_encode_query_value(asset_type) +
                           "&signature_type=" + std::to_string(static_cast<int>(sig_type_));
        if (!token_id.empty())
            path += "&token_id=" + percent_encode_query_value(token_id);
        auto response =
            read([&] { return http_.get(path, get_l2_headers("GET", request_path, "")); });
        return response.ok();
    }
}
