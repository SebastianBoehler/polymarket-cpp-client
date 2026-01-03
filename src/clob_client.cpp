#include "clob_client.hpp"
#include "order_signer.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <memory>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <chrono>

using json = nlohmann::json;

namespace polymarket
{

    // Exchange addresses for Polygon mainnet
    static const std::string EXCHANGE_ADDRESS = "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E";
    static const std::string NEG_RISK_EXCHANGE_ADDRESS = "0xC5d563A36AE78145C45a50134d48A1215220f80a";

    ClobClient::ClobClient(const std::string &base_url, int chain_id)
        : chain_id_(chain_id), base_url_(base_url), sig_type_(SignatureType::EOA)
    {
        http_.set_base_url(base_url);
        http_.set_timeout_ms(10000);
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key,
                           const ApiCredentials &creds,
                           SignatureType sig_type,
                           const std::string &funder_address)
        : chain_id_(chain_id), base_url_(base_url), funder_address_(funder_address), sig_type_(sig_type)
    {
        http_.set_base_url(base_url);
        http_.set_timeout_ms(10000);

        order_signer_ = std::make_unique<OrderSigner>(private_key, chain_id);
        api_creds_ = std::make_unique<ApiCredentials>(creds);
    }

    ClobClient::~ClobClient() = default;

    std::string ClobClient::get_exchange_address() const
    {
        return EXCHANGE_ADDRESS;
    }

    std::string ClobClient::get_neg_risk_exchange_address() const
    {
        return NEG_RISK_EXCHANGE_ADDRESS;
    }

    std::string ClobClient::get_address() const
    {
        if (!order_signer_)
            return "";
        return order_signer_->address();
    }

    std::map<std::string, std::string> ClobClient::get_l2_headers(const std::string &method,
                                                                  const std::string &path,
                                                                  const std::string &body)
    {
        if (!order_signer_ || !api_creds_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        auto headers = order_signer_->generate_l2_headers(*api_creds_, method, path, body, funder_address_);

        std::map<std::string, std::string> result;
        result["POLY_ADDRESS"] = headers.poly_address;
        result["POLY_SIGNATURE"] = headers.poly_signature;
        result["POLY_TIMESTAMP"] = headers.poly_timestamp;
        result["POLY_API_KEY"] = headers.poly_api_key;
        result["POLY_PASSPHRASE"] = headers.poly_passphrase;

        return result;
    }

    std::string ClobClient::order_type_to_string(OrderType type)
    {
        switch (type)
        {
        case OrderType::GTC:
            return "GTC";
        case OrderType::GTD:
            return "GTD";
        case OrderType::FOK:
            return "FOK";
        case OrderType::FAK:
            return "IOC";
        default:
            return "GTC";
        }
    }

    std::string ClobClient::order_side_to_string(OrderSide side)
    {
        return side == OrderSide::BUY ? "BUY" : "SELL";
    }

    // ============================================================
    // PUBLIC ENDPOINTS
    // ============================================================

    std::optional<uint64_t> ClobClient::get_server_time()
    {
        auto response = http_.get("/time");
        if (!response.ok())
            return std::nullopt;

        try
        {
            // Server returns plain timestamp, not JSON
            return std::stoull(response.body);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<ClobMarket> ClobClient::get_markets(const std::string &next_cursor)
    {
        std::string path = "/markets";
        if (!next_cursor.empty())
        {
            path += "?next_cursor=" + next_cursor;
        }

        auto response = http_.get(path);
        if (!response.ok())
            return {};

        return parse_markets(response.body);
    }

    std::optional<ClobMarket> ClobClient::get_market(const std::string &condition_id)
    {
        auto response = http_.get("/markets/" + condition_id);
        if (!response.ok())
            return std::nullopt;

        auto markets = parse_markets("[" + response.body + "]");
        if (markets.empty())
            return std::nullopt;

        return markets[0];
    }

    std::vector<ClobMarket> ClobClient::get_sampling_markets(const std::string &next_cursor)
    {
        std::string path = "/sampling-markets";
        if (!next_cursor.empty())
        {
            path += "?next_cursor=" + next_cursor;
        }

        auto response = http_.get(path);
        if (!response.ok())
            return {};

        return parse_markets(response.body);
    }

    std::vector<ClobMarket> ClobClient::get_simplified_markets(const std::string &next_cursor)
    {
        std::string path = "/simplified-markets";
        if (!next_cursor.empty())
        {
            path += "?next_cursor=" + next_cursor;
        }

        auto response = http_.get(path);
        if (!response.ok())
            return {};

        return parse_markets(response.body);
    }

    std::vector<ClobMarket> ClobClient::get_sampling_simplified_markets(const std::string &next_cursor)
    {
        std::string path = "/sampling-simplified-markets";
        if (!next_cursor.empty())
        {
            path += "?next_cursor=" + next_cursor;
        }

        auto response = http_.get(path);
        if (!response.ok())
            return {};

        return parse_markets(response.body);
    }

    std::optional<Orderbook> ClobClient::get_order_book(const std::string &token_id)
    {
        auto response = http_.get("/book?token_id=" + token_id);
        if (!response.ok())
            return std::nullopt;

        return parse_orderbook(response.body);
    }

    std::map<std::string, Orderbook> ClobClient::get_order_books(const std::vector<std::string> &token_ids)
    {
        std::map<std::string, Orderbook> result;

        // Build comma-separated token IDs
        std::string ids;
        for (size_t i = 0; i < token_ids.size(); i++)
        {
            if (i > 0)
                ids += ",";
            ids += token_ids[i];
        }

        auto response = http_.get("/books?token_ids=" + ids);
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
                {
                    if (item.contains("asset_id"))
                    {
                        auto book = parse_orderbook(item.dump());
                        if (book)
                        {
                            result[item["asset_id"].get<std::string>()] = *book;
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::optional<PriceInfo> ClobClient::get_price(const std::string &token_id, const std::string &side)
    {
        auto response = http_.get("/price?token_id=" + token_id + "&side=" + side);
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            PriceInfo info;
            info.token_id = token_id;
            info.price = std::stod(j.value("price", "0"));
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

        std::string ids;
        for (size_t i = 0; i < token_ids.size(); i++)
        {
            if (i > 0)
                ids += ",";
            ids += token_ids[i];
        }

        auto response = http_.get("/prices?token_ids=" + ids + "&side=" + side);
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (size_t i = 0; i < j.size() && i < token_ids.size(); i++)
                {
                    PriceInfo info;
                    info.token_id = token_ids[i];
                    info.price = std::stod(j[i].value("price", "0"));
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::optional<PriceInfo> ClobClient::get_last_trade_price(const std::string &token_id)
    {
        auto response = http_.get("/last-trade-price?token_id=" + token_id);
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            PriceInfo info;
            info.token_id = token_id;
            info.price = std::stod(j.value("price", "0"));
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

        std::string ids;
        for (size_t i = 0; i < token_ids.size(); i++)
        {
            if (i > 0)
                ids += ",";
            ids += token_ids[i];
        }

        auto response = http_.get("/last-trades-prices?token_ids=" + ids);
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (size_t i = 0; i < j.size() && i < token_ids.size(); i++)
                {
                    PriceInfo info;
                    info.token_id = token_ids[i];
                    info.price = std::stod(j[i].value("price", "0"));
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::optional<MidpointInfo> ClobClient::get_midpoint(const std::string &token_id)
    {
        auto response = http_.get("/midpoint?token_id=" + token_id);
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            MidpointInfo info;
            info.token_id = token_id;
            info.mid = std::stod(j.value("mid", "0"));
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

        std::string ids;
        for (size_t i = 0; i < token_ids.size(); i++)
        {
            if (i > 0)
                ids += ",";
            ids += token_ids[i];
        }

        auto response = http_.get("/midpoints?token_ids=" + ids);
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (size_t i = 0; i < j.size() && i < token_ids.size(); i++)
                {
                    MidpointInfo info;
                    info.token_id = token_ids[i];
                    info.mid = std::stod(j[i].value("mid", "0"));
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::optional<SpreadInfo> ClobClient::get_spread(const std::string &token_id)
    {
        auto response = http_.get("/spread?token_id=" + token_id);
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            SpreadInfo info;
            info.token_id = token_id;
            info.spread = std::stod(j.value("spread", "0"));
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

        std::string ids;
        for (size_t i = 0; i < token_ids.size(); i++)
        {
            if (i > 0)
                ids += ",";
            ids += token_ids[i];
        }

        auto response = http_.get("/spreads?token_ids=" + ids);
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (size_t i = 0; i < j.size() && i < token_ids.size(); i++)
                {
                    SpreadInfo info;
                    info.token_id = token_ids[i];
                    info.spread = std::stod(j[i].value("spread", "0"));
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::optional<TickSizeInfo> ClobClient::get_tick_size(const std::string &token_id)
    {
        auto response = http_.get("/tick-size?token_id=" + token_id);
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            TickSizeInfo info;
            info.minimum_tick_size = j.value("minimum_tick_size", "0.01");
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<NegRiskInfo> ClobClient::get_neg_risk(const std::string &token_id)
    {
        auto response = http_.get("/neg-risk?token_id=" + token_id);
        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            NegRiskInfo info;
            info.neg_risk = j.value("neg_risk", false);
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<ClobClient::PriceHistoryPoint> ClobClient::get_prices_history(
        const std::string &token_id,
        uint64_t start_ts,
        uint64_t end_ts,
        const std::string &interval,
        const std::string &fidelity)
    {
        std::vector<PriceHistoryPoint> result;

        std::string path = "/prices-history?token_id=" + token_id;
        if (start_ts > 0)
            path += "&startTs=" + std::to_string(start_ts);
        if (end_ts > 0)
            path += "&endTs=" + std::to_string(end_ts);
        path += "&interval=" + interval;
        path += "&fidelity=" + fidelity;

        auto response = http_.get(path);
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.contains("history") && j["history"].is_array())
            {
                for (const auto &item : j["history"])
                {
                    PriceHistoryPoint point;
                    point.timestamp = item.value("t", 0ULL);
                    point.price = std::stod(item.value("p", "0"));
                    result.push_back(point);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::vector<Trade> ClobClient::get_market_trades_events(const std::string &condition_id,
                                                            const std::string &next_cursor)
    {
        std::string path = "/trades?market=" + condition_id;
        if (!next_cursor.empty())
        {
            path += "&next_cursor=" + next_cursor;
        }

        auto response = http_.get(path);
        if (!response.ok())
            return {};

        return parse_trades(response.body);
    }

    // ============================================================
    // AUTHENTICATED ENDPOINTS (L1)
    // ============================================================

    ApiCredentials ClobClient::create_api_key(uint64_t nonce)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }
        return order_signer_->create_api_credentials(http_, nonce, funder_address_);
    }

    ApiCredentials ClobClient::derive_api_key()
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }
        return order_signer_->derive_api_credentials(http_, funder_address_);
    }

    ApiCredentials ClobClient::create_or_derive_api_key()
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }
        return order_signer_->create_or_derive_api_credentials(http_, funder_address_);
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
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
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

        // Need to add DELETE method to HttpClient - for now use POST with method override
        // This is a limitation - would need to extend HttpClient
        return false; // TODO: Implement DELETE method
    }

    // ============================================================
    // AUTHENTICATED ENDPOINTS (L2 - Trading)
    // ============================================================

    SignedOrder ClobClient::create_order(const CreateOrderParams &params)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        // Determine exchange address based on neg_risk (makes HTTP call)
        auto neg_risk_info = get_neg_risk(params.token_id);
        bool is_neg_risk = neg_risk_info && neg_risk_info->neg_risk;

        return create_order(params, is_neg_risk);
    }

    SignedOrder ClobClient::create_order(const CreateOrderParams &params, bool is_neg_risk)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        // Use cached neg_risk value to determine exchange address
        std::string exchange_addr = is_neg_risk ? NEG_RISK_EXCHANGE_ADDRESS : EXCHANGE_ADDRESS;

        // Calculate amounts
        double maker_amount, taker_amount;
        if (params.side == OrderSide::BUY)
        {
            // BUY: maker pays USDC, receives shares
            maker_amount = params.size * params.price;
            taker_amount = params.size;
        }
        else
        {
            // SELL: maker pays shares, receives USDC
            maker_amount = params.size;
            taker_amount = params.size * params.price;
        }

        OrderData order_data;
        order_data.maker = funder_address_.empty() ? order_signer_->address() : funder_address_;
        order_data.taker = "0x0000000000000000000000000000000000000000";
        order_data.token_id = params.token_id;
        order_data.maker_amount = to_wei(maker_amount, 6);
        order_data.taker_amount = to_wei(taker_amount, 6);
        order_data.side = params.side;
        order_data.fee_rate_bps = params.fee_rate_bps;
        order_data.nonce = params.nonce;
        order_data.signer = order_signer_->address();
        order_data.expiration = params.expiration;
        order_data.signature_type = sig_type_;

        return order_signer_->sign_order(order_data, exchange_addr);
    }

    SignedOrder ClobClient::create_market_order(const CreateMarketOrderParams &params)
    {
        if (!order_signer_)
        {
            throw std::runtime_error("Client not authenticated");
        }

        // Get current price if not specified
        double price = 0.5;
        if (params.price)
        {
            price = *params.price;
        }
        else
        {
            auto price_info = get_price(params.token_id, params.side == OrderSide::BUY ? "buy" : "sell");
            if (price_info)
            {
                price = price_info->price;
            }
        }

        // Calculate size from amount
        double size;
        if (params.side == OrderSide::BUY)
        {
            size = params.amount / price;
        }
        else
        {
            size = params.amount;
        }

        CreateOrderParams order_params;
        order_params.token_id = params.token_id;
        order_params.price = price;
        order_params.size = size;
        order_params.side = params.side;

        return create_order(order_params);
    }

    OrderResponse ClobClient::post_order(const SignedOrder &order, OrderType order_type)
    {
        json body;
        body["order"] = {
            {"salt", order.salt},
            {"maker", order.maker},
            {"signer", order.signer},
            {"taker", order.taker},
            {"tokenId", order.token_id},
            {"makerAmount", order.maker_amount},
            {"takerAmount", order.taker_amount},
            {"expiration", order.expiration},
            {"nonce", order.nonce},
            {"feeRateBps", order.fee_rate_bps},
            {"side", order.side == 0 ? "BUY" : "SELL"},
            {"signatureType", order.signature_type},
            {"signature", order.signature}};
        body["orderType"] = order_type_to_string(order_type);

        std::string body_str = body.dump();
        auto headers = get_l2_headers("POST", "/order", body_str);
        auto response = http_.post("/order", body_str, headers);

        return parse_order_response(response.body);
    }

    std::vector<OrderResponse> ClobClient::post_orders(const std::vector<BatchOrderEntry> &orders)
    {
        std::vector<OrderResponse> results;

        if (orders.empty())
            return results;

        json body = json::array();
        for (const auto &entry : orders)
        {
            json order_json;
            order_json["order"] = {
                {"salt", entry.order.salt},
                {"maker", entry.order.maker},
                {"signer", entry.order.signer},
                {"taker", entry.order.taker},
                {"tokenId", entry.order.token_id},
                {"makerAmount", entry.order.maker_amount},
                {"takerAmount", entry.order.taker_amount},
                {"expiration", entry.order.expiration},
                {"nonce", entry.order.nonce},
                {"feeRateBps", entry.order.fee_rate_bps},
                {"side", entry.order.side == 0 ? "BUY" : "SELL"},
                {"signatureType", entry.order.signature_type},
                {"signature", entry.order.signature}};
            order_json["orderType"] = order_type_to_string(entry.order_type);
            body.push_back(order_json);
        }

        std::string body_str = body.dump();
        auto headers = get_l2_headers("POST", "/orders", body_str);
        auto response = http_.post("/orders", body_str, headers);

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
                {
                    results.push_back(parse_order_response(item.dump()));
                }
            }
        }
        catch (...)
        {
            // Single error response
            results.push_back(parse_order_response(response.body));
        }

        return results;
    }

    OrderResponse ClobClient::create_and_post_order(const CreateOrderParams &params, OrderType order_type)
    {
        auto signed_order = create_order(params);
        return post_order(signed_order, order_type);
    }

    OrderResponse ClobClient::create_and_post_market_order(const CreateMarketOrderParams &params, OrderType order_type)
    {
        auto signed_order = create_market_order(params);
        return post_order(signed_order, order_type);
    }

    bool ClobClient::cancel_order(const std::string &order_id)
    {
        json body;
        body["orderID"] = order_id;

        std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/order", body_str);

        // Use POST with body for cancel (API accepts this)
        auto response = http_.post("/order", body_str, headers);
        return response.ok();
    }

    bool ClobClient::cancel_orders(const std::vector<std::string> &order_ids)
    {
        json body = order_ids;

        std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/orders", body_str);

        auto response = http_.post("/orders", body_str, headers);
        return response.ok();
    }

    bool ClobClient::cancel_all()
    {
        auto headers = get_l2_headers("DELETE", "/cancel-all", "");
        auto response = http_.post("/cancel-all", "{}", headers);
        return response.ok();
    }

    bool ClobClient::cancel_market_orders(const std::string &condition_id)
    {
        json body;
        body["market"] = condition_id;

        std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/cancel-market-orders", body_str);

        auto response = http_.post("/cancel-market-orders", body_str, headers);
        return response.ok();
    }

    std::optional<OpenOrder> ClobClient::get_order(const std::string &order_id)
    {
        auto headers = get_l2_headers("GET", "/order/" + order_id, "");
        auto response = http_.get("/order/" + order_id, headers);

        if (!response.ok())
            return std::nullopt;

        auto orders = parse_open_orders("[" + response.body + "]");
        if (orders.empty())
            return std::nullopt;

        return orders[0];
    }

    std::vector<OpenOrder> ClobClient::get_open_orders(const std::string &market)
    {
        std::string path = "/orders";
        if (!market.empty())
        {
            path += "?market=" + market;
        }

        auto headers = get_l2_headers("GET", path, "");
        auto response = http_.get(path, headers);

        if (!response.ok())
            return {};

        return parse_open_orders(response.body);
    }

    std::vector<Trade> ClobClient::get_trades(const std::string &next_cursor)
    {
        std::string path = "/trades";
        if (!next_cursor.empty())
        {
            path += "?next_cursor=" + next_cursor;
        }

        auto headers = get_l2_headers("GET", path, "");
        auto response = http_.get(path, headers);

        if (!response.ok())
            return {};

        return parse_trades(response.body);
    }

    std::optional<BalanceAllowance> ClobClient::get_balance_allowance(const std::string &asset_type)
    {
        std::string path = "/balance-allowance?asset_type=" + asset_type;
        auto headers = get_l2_headers("GET", path, "");
        auto response = http_.get(path, headers);

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            BalanceAllowance ba;
            ba.balance = j.value("balance", "0");
            ba.allowance = j.value("allowance", "0");
            return ba;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ClobClient::update_balance_allowance(const std::string &asset_type)
    {
        json body;
        body["asset_type"] = asset_type;

        std::string body_str = body.dump();
        auto headers = get_l2_headers("POST", "/balance-allowance", body_str);

        auto response = http_.post("/balance-allowance", body_str, headers);
        return response.ok();
    }

    std::optional<OrderScoringResult> ClobClient::is_order_scoring(const SignedOrder &order)
    {
        json body;
        body["orderId"] = order.salt; // Use salt as order identifier for scoring check

        std::string body_str = body.dump();
        auto response = http_.post("/order-scoring", body_str);

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            OrderScoringResult result;
            result.scoring = j.value("scoring", false);
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<OrderScoringResult> ClobClient::are_orders_scoring(const std::vector<SignedOrder> &orders)
    {
        std::vector<OrderScoringResult> results;

        json body = json::array();
        for (const auto &order : orders)
        {
            body.push_back({{"orderId", order.salt}});
        }

        std::string body_str = body.dump();
        auto response = http_.post("/orders-scoring", body_str);

        if (!response.ok())
            return results;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
                {
                    OrderScoringResult result;
                    result.scoring = item.value("scoring", false);
                    results.push_back(result);
                }
            }
        }
        catch (...)
        {
        }

        return results;
    }

    std::vector<ClobClient::Notification> ClobClient::get_notifications()
    {
        std::vector<Notification> result;

        auto headers = get_l2_headers("GET", "/notifications", "");
        auto response = http_.get("/notifications", headers);

        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
                {
                    Notification n;
                    n.id = item.value("id", "");
                    n.type = item.value("type", "");
                    n.message = item.value("message", "");
                    n.created_at = item.value("createdAt", "");
                    result.push_back(n);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    bool ClobClient::drop_notifications(const std::vector<std::string> &notification_ids)
    {
        json body = notification_ids;

        std::string body_str = body.dump();
        auto headers = get_l2_headers("DELETE", "/notifications", body_str);

        auto response = http_.post("/notifications", body_str, headers);
        return response.ok();
    }

    std::vector<ClobClient::RewardsInfo> ClobClient::get_rewards_markets_current()
    {
        std::vector<RewardsInfo> result;

        auto response = http_.get("/rewards/markets/current");
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
                {
                    RewardsInfo info;
                    info.market = item.value("market", "");
                    info.min_size = item.value("minSize", "");
                    info.max_spread = item.value("maxSpread", "");
                    info.reward_epoch = item.value("rewardEpoch", "");
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::vector<ClobClient::RewardsInfo> ClobClient::get_rewards_markets(const std::string &epoch)
    {
        std::vector<RewardsInfo> result;

        std::string path = "/rewards/markets";
        if (!epoch.empty())
        {
            path += "?epoch=" + epoch;
        }

        auto response = http_.get(path);
        if (!response.ok())
            return result;

        try
        {
            auto j = json::parse(response.body);
            if (j.is_array())
            {
                for (const auto &item : j)
                {
                    RewardsInfo info;
                    info.market = item.value("market", "");
                    info.min_size = item.value("minSize", "");
                    info.max_spread = item.value("maxSpread", "");
                    info.reward_epoch = item.value("rewardEpoch", "");
                    result.push_back(info);
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::optional<ClobClient::EarningsInfo> ClobClient::get_earnings_for_user_for_day(const std::string &date)
    {
        std::string path = "/rewards/earnings";
        if (!date.empty())
        {
            path += "?date=" + date;
        }

        auto headers = get_l2_headers("GET", path, "");
        auto response = http_.get(path, headers);

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            EarningsInfo info;
            info.market = j.value("market", "");
            info.earnings = j.value("earnings", "0");
            info.epoch = j.value("epoch", "");
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<ClobClient::EarningsInfo> ClobClient::get_total_earnings_for_user_for_day(const std::string &date)
    {
        std::string path = "/rewards/total-earnings";
        if (!date.empty())
        {
            path += "?date=" + date;
        }

        auto headers = get_l2_headers("GET", path, "");
        auto response = http_.get(path, headers);

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            EarningsInfo info;
            info.earnings = j.value("earnings", "0");
            info.epoch = j.value("epoch", "");
            return info;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<ClobClient::FeeRateInfo> ClobClient::get_fee_rate()
    {
        auto headers = get_l2_headers("GET", "/fee-rate", "");
        auto response = http_.get("/fee-rate", headers);

        if (!response.ok())
            return std::nullopt;

        try
        {
            auto j = json::parse(response.body);
            FeeRateInfo info;
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
    // JSON PARSING HELPERS
    // ============================================================

    std::vector<ClobMarket> ClobClient::parse_markets(const std::string &json_str)
    {
        std::vector<ClobMarket> markets;

        try
        {
            auto j = json::parse(json_str);

            json market_array;
            if (j.is_array())
            {
                market_array = j;
            }
            else if (j.contains("data") && j["data"].is_array())
            {
                market_array = j["data"];
            }
            else
            {
                return markets;
            }

            for (const auto &item : market_array)
            {
                ClobMarket market;

                if (item.contains("condition_id"))
                {
                    market.condition_id = item["condition_id"].get<std::string>();
                }
                if (item.contains("question") && !item["question"].is_null())
                {
                    market.question = item["question"].get<std::string>();
                }
                if (item.contains("market_slug") && !item["market_slug"].is_null())
                {
                    market.market_slug = item["market_slug"].get<std::string>();
                }
                if (item.contains("neg_risk"))
                {
                    market.neg_risk = item["neg_risk"].get<bool>();
                }
                if (item.contains("active"))
                {
                    market.active = item["active"].get<bool>();
                }
                if (item.contains("closed"))
                {
                    market.closed = item["closed"].get<bool>();
                }

                if (item.contains("tokens") && item["tokens"].is_array())
                {
                    for (const auto &t : item["tokens"])
                    {
                        Token token;
                        if (t.contains("token_id"))
                        {
                            token.token_id = t["token_id"].get<std::string>();
                        }
                        if (t.contains("outcome"))
                        {
                            token.outcome = t["outcome"].get<std::string>();
                        }
                        market.tokens.push_back(token);
                    }
                }

                markets.push_back(std::move(market));
            }
        }
        catch (...)
        {
        }

        return markets;
    }

    std::optional<Orderbook> ClobClient::parse_orderbook(const std::string &json_str)
    {
        try
        {
            auto j = json::parse(json_str);

            Orderbook book;
            book.timestamp_ns = now_ns();

            if (j.contains("asset_id"))
            {
                book.asset_id = j["asset_id"].get<std::string>();
            }

            if (j.contains("bids") && j["bids"].is_array())
            {
                for (const auto &bid : j["bids"])
                {
                    PriceLevel level;
                    level.price = std::stod(bid["price"].get<std::string>());
                    level.size = std::stod(bid["size"].get<std::string>());
                    book.bids.push_back(level);
                }
            }

            if (j.contains("asks") && j["asks"].is_array())
            {
                for (const auto &ask : j["asks"])
                {
                    PriceLevel level;
                    level.price = std::stod(ask["price"].get<std::string>());
                    level.size = std::stod(ask["size"].get<std::string>());
                    book.asks.push_back(level);
                }
            }

            return book;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    OrderResponse ClobClient::parse_order_response(const std::string &json_str)
    {
        OrderResponse result;
        result.success = false;

        try
        {
            auto j = json::parse(json_str);

            result.success = j.value("success", false);
            result.error_msg = j.value("errorMsg", "");
            result.order_id = j.value("orderID", "");
            result.status = j.value("status", "");
            result.taking_amount = j.value("takingAmount", "0");
            result.making_amount = j.value("makingAmount", "0");

            if (j.contains("transactionsHashes") && j["transactionsHashes"].is_array())
            {
                for (const auto &hash : j["transactionsHashes"])
                {
                    result.transaction_hashes.push_back(hash.get<std::string>());
                }
            }
        }
        catch (...)
        {
        }

        return result;
    }

    std::vector<OpenOrder> ClobClient::parse_open_orders(const std::string &json_str)
    {
        std::vector<OpenOrder> orders;

        try
        {
            auto j = json::parse(json_str);

            json order_array;
            if (j.is_array())
            {
                order_array = j;
            }
            else if (j.contains("data") && j["data"].is_array())
            {
                order_array = j["data"];
            }
            else
            {
                return orders;
            }

            for (const auto &item : order_array)
            {
                OpenOrder order;
                order.id = item.value("id", "");
                order.market = item.value("market", "");
                order.asset_id = item.value("asset_id", "");
                order.side = item.value("side", "");
                order.original_size = item.value("original_size", "0");
                order.size_matched = item.value("size_matched", "0");
                order.price = item.value("price", "0");
                order.status = item.value("status", "");
                order.created_at = item.value("created_at", "");
                order.expiration = item.value("expiration", "0");
                order.order_type = item.value("order_type", "GTC");
                orders.push_back(order);
            }
        }
        catch (...)
        {
        }

        return orders;
    }

    std::vector<Trade> ClobClient::parse_trades(const std::string &json_str)
    {
        std::vector<Trade> trades;

        try
        {
            auto j = json::parse(json_str);

            json trade_array;
            if (j.is_array())
            {
                trade_array = j;
            }
            else if (j.contains("data") && j["data"].is_array())
            {
                trade_array = j["data"];
            }
            else
            {
                return trades;
            }

            for (const auto &item : trade_array)
            {
                Trade trade;
                trade.id = item.value("id", "");
                trade.market = item.value("market", "");
                trade.asset_id = item.value("asset_id", "");
                trade.side = item.value("side", "");
                trade.size = item.value("size", "0");
                trade.price = item.value("price", "0");
                trade.fee_rate_bps = item.value("fee_rate_bps", "0");
                trade.status = item.value("status", "");
                trade.created_at = item.value("created_at", "");
                trade.match_time = item.value("match_time", "");
                trade.transaction_hash = item.value("transaction_hash", "");
                trades.push_back(trade);
            }
        }
        catch (...)
        {
        }

        return trades;
    }

} // namespace polymarket
