#include "clob_client.hpp"
#include "clob_client_internal.hpp"
#include "market_fetcher.hpp"
#include "order_signer.hpp"
#include "order_signer_auth_internal.hpp"

#include <nlohmann/json.hpp>
#include <charconv>
#include <chrono>
#include <memory>
#include <stdexcept>

using json = nlohmann::json;

namespace polymarket
{
    using detail::percent_encode_query_value;

    // Exchange addresses for Polygon mainnet
    static const std::string EXCHANGE_ADDRESS = "0xE111180000d2663C0091e4f400237545B87B996B";
    static const std::string NEG_RISK_EXCHANGE_ADDRESS = "0xe2222d279d744050d28e00520010520000310F59";
    static constexpr const char *DATA_API_URL = "https://data-api.polymarket.com";

    static int validated_chain_id(int chain_id)
    {
        if (chain_id != 137 && chain_id != 80002)
            throw std::invalid_argument("unsupported CLOB chain ID");
        return chain_id;
    }

    static std::string validated_funder(SignatureType signature_type,
                                        const std::string &funder)
    {
        switch (signature_type)
        {
        case SignatureType::EOA:
            return funder;
        case SignatureType::POLY_PROXY:
        case SignatureType::POLY_GNOSIS_SAFE:
        case SignatureType::POLY_1271:
            if (!funder.empty()) return funder;
            throw std::invalid_argument(
                "non-EOA signature types require a funder address");
        }
        throw std::invalid_argument("unsupported signature type");
    }

    static void configure_default_transports(HttpClient &clob, HttpClient &data,
                                             const std::string &clob_url)
    {
        clob.set_base_url(clob_url);
        data.set_base_url(DATA_API_URL);
        clob.set_timeout_ms(10000);
        data.set_timeout_ms(10000);
    }

    static void configure_transports(HttpClient &clob, HttpClient &data,
                                     const std::string &clob_url,
                                     const HttpClientOptions &options)
    {
        clob.configure(options);
        data.configure(options);
        clob.set_base_url(clob_url);
        data.set_base_url(DATA_API_URL);
    }

    static ClobMarketPage fetch_market_page(
        HttpClient &http, const std::string &endpoint,
        const std::string &next_cursor)
    {
        std::string path = endpoint;
        if (!next_cursor.empty())
            path += "?next_cursor=" + percent_encode_query_value(next_cursor);
        const auto response = http.get(path);
        if (!response.ok()) return {};
        try
        {
            return detail::parse_clob_market_page_json(response.body);
        }
        catch (...)
        {
            return {};
        }
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id)
        : chain_id_(validated_chain_id(chain_id)), base_url_(base_url), sig_type_(SignatureType::EOA)
    {
        configure_default_transports(http_, data_http_, base_url);
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id, const HttpClientOptions &http_options)
        : chain_id_(validated_chain_id(chain_id)), base_url_(base_url), sig_type_(SignatureType::EOA)
    {
        configure_transports(http_, data_http_, base_url, http_options);
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key, SignatureType sig_type,
                           const std::string &funder_address)
        : chain_id_(validated_chain_id(chain_id)), base_url_(base_url),
          funder_address_(validated_funder(sig_type, funder_address)),
          sig_type_(sig_type)
    {
        configure_default_transports(http_, data_http_, base_url);
        order_signer_ = std::make_unique<OrderSigner>(private_key, chain_id);
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key, SignatureType sig_type,
                           const std::string &funder_address,
                           const HttpClientOptions &http_options)
        : chain_id_(validated_chain_id(chain_id)), base_url_(base_url),
          funder_address_(validated_funder(sig_type, funder_address)),
          sig_type_(sig_type)
    {
        configure_transports(http_, data_http_, base_url, http_options);
        order_signer_ = std::make_unique<OrderSigner>(private_key, chain_id);
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key,
                           const ApiCredentials &creds,
                           SignatureType sig_type,
                           const std::string &funder_address)
        : chain_id_(validated_chain_id(chain_id)), base_url_(base_url),
          funder_address_(validated_funder(sig_type, funder_address)),
          sig_type_(sig_type)
    {
        configure_default_transports(http_, data_http_, base_url);

        order_signer_ = std::make_unique<OrderSigner>(private_key, chain_id);
        detail::validate_api_credentials(creds);
        api_creds_ = std::make_unique<ApiCredentials>(creds);
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key,
                           const ApiCredentials &creds,
                           SignatureType sig_type,
                           const std::string &funder_address,
                           const HttpClientOptions &http_options)
        : chain_id_(validated_chain_id(chain_id)), base_url_(base_url),
          funder_address_(validated_funder(sig_type, funder_address)),
          sig_type_(sig_type)
    {
        configure_transports(http_, data_http_, base_url, http_options);

        order_signer_ = std::make_unique<OrderSigner>(private_key, chain_id);
        detail::validate_api_credentials(creds);
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

    bool ClobClient::warm_connection()
    {
        return get_server_time().has_value();
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
            return "FAK";
        default:
            throw std::invalid_argument("Invalid order type");
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

        uint64_t timestamp = 0;
        const auto begin = response.body.data();
        const auto end = begin + response.body.size();
        const auto parsed = std::from_chars(begin, end, timestamp);
        constexpr uint64_t unix_seconds_2000 = 946'684'800ULL;
        constexpr uint64_t unix_seconds_9999 = 253'402'300'799ULL;
        if (response.body.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != end || timestamp < unix_seconds_2000 ||
            timestamp > unix_seconds_9999)
            return std::nullopt;
        return timestamp;
    }

    ClobMarketPage ClobClient::get_markets(const std::string &next_cursor)
    {
        return fetch_market_page(http_, "/markets", next_cursor);
    }

    std::optional<ClobMarket> ClobClient::get_market(const std::string &condition_id)
    {
        if (condition_id.empty()) return std::nullopt;
        auto response = http_.get("/markets/" + condition_id);
        if (!response.ok())
            return std::nullopt;

        auto markets = parse_markets("[" + response.body + "]");
        if (markets.size() != 1 || markets[0].condition_id != condition_id)
            return std::nullopt;

        return markets[0];
    }

    ClobMarketPage ClobClient::get_sampling_markets(
        const std::string &next_cursor)
    {
        return fetch_market_page(http_, "/sampling-markets", next_cursor);
    }

    ClobMarketPage ClobClient::get_simplified_markets(
        const std::string &next_cursor)
    {
        return fetch_market_page(http_, "/simplified-markets", next_cursor);
    }

    ClobMarketPage ClobClient::get_sampling_simplified_markets(
        const std::string &next_cursor)
    {
        return fetch_market_page(http_, "/sampling-simplified-markets",
                                 next_cursor);
    }
}
