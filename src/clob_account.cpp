#include "clob_client.hpp"
#include "clob_client_internal.hpp"
#include "opaque_cursor_pagination.hpp"

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

        auto headers = get_l2_headers("GET", "/auth/api-keys", "");
        auto response = http_.get("/auth/api-keys", headers);

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

        auto headers = get_l2_headers("GET", endpoint, "");
        auto response = http_.get(endpoint, headers);

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
        constexpr const char *endpoint = "/data/orders";
        if (!order_signer_ || !api_creds_)
        {
            return Result<std::vector<OpenOrder>>::failure(make_auth_error("Client not authenticated", endpoint));
        }

        std::vector<OpenOrder> orders;
        OpaqueCursorPagination pagination(INITIAL_CURSOR, END_CURSOR);
        while (true)
        {
            try
            {
                if (!pagination.begin_page())
                    break;
            }
            catch (const OpaqueCursorPaginationError &ex)
            {
                return Result<std::vector<OpenOrder>>::failure(make_parse_error(ex.what(), endpoint));
            }

            std::string path = endpoint;
            path += market.empty() ? "?" : "?market=" + percent_encode_query_value(market) + "&";
            path += "next_cursor=" + percent_encode_query_value(pagination.cursor());

            auto headers = get_l2_headers("GET", endpoint, "");
            auto response = http_.get(path, headers);

            if (!response.ok())
            {
                return Result<std::vector<OpenOrder>>::failure(make_sdk_error(response, endpoint));
            }

            try
            {
                auto page = detail::parse_open_order_page_json(response.body);
                orders.insert(orders.end(), page.orders.begin(), page.orders.end());
                pagination.advance(std::move(page.next_cursor));
            }
            catch (const std::exception &ex)
            {
                return Result<std::vector<OpenOrder>>::failure(make_parse_error(ex.what(), endpoint, response.body));
            }
        }

        return Result<std::vector<OpenOrder>>::success(std::move(orders));
    }

    std::vector<Trade> ClobClient::get_trades(const std::string &next_cursor)
    {
        constexpr const char *endpoint = "/data/trades";
        std::vector<Trade> trades;
        OpaqueCursorPagination pagination(next_cursor.empty() ? INITIAL_CURSOR : next_cursor,
                                          END_CURSOR);
        while (true)
        {
            try
            {
                if (!pagination.begin_page())
                    break;
            }
            catch (const OpaqueCursorPaginationError &)
            {
                return {};
            }

            const std::string path = std::string(endpoint) +
                                     "?next_cursor=" + percent_encode_query_value(pagination.cursor());
            auto headers = get_l2_headers("GET", endpoint, "");
            auto response = http_.get(path, headers);

            if (!response.ok())
                return {};

            try
            {
                auto page = detail::parse_trade_page_json(response.body);
                trades.insert(trades.end(), page.trades.begin(), page.trades.end());
                pagination.advance(std::move(page.next_cursor));
            }
            catch (...)
            {
                return {};
            }
        }

        return trades;
    }

    std::optional<BalanceAllowance> ClobClient::get_balance_allowance(
        const std::string &asset_type, const std::string &token_id)
    {
        std::string request_path = "/balance-allowance";
        std::string path = request_path + "?asset_type=" + percent_encode_query_value(asset_type) +
                           "&signature_type=" + std::to_string(static_cast<int>(sig_type_));
        if (!token_id.empty())
            path += "&token_id=" + percent_encode_query_value(token_id);
        auto headers = get_l2_headers("GET", request_path, "");
        auto response = http_.get(path, headers);

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
        auto headers = get_l2_headers("GET", request_path, "");
        auto response = http_.get(path, headers);
        return response.ok();
    }
}
