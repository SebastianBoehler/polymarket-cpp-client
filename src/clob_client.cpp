#include "polymarket/clob_client.hpp"
#include "clob_client_internal.hpp"
#include "polymarket/market_fetcher.hpp"
#include "polymarket/order_signer.hpp"
#include "order_signer_auth_internal.hpp"
#include "polymarket/polymarket_contracts.hpp"

#include <nlohmann/json.hpp>
#include <charconv>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

namespace polymarket
{
    using detail::percent_encode_query_value;

    static Environment legacy_environment(const std::string &base_url, int chain_id)
    {
        if (chain_id != 137)
            throw std::invalid_argument("unsupported CLOB chain ID " + std::to_string(chain_id) +
                                        "; use an Environment for other chains");
        Environment environment = Environment::production();
        environment.clob_url = base_url;
        return environment;
    }

    static Environment validated_environment(const Environment &environment)
    {
        environment.validate();
        if (environment.contracts.chain_id > static_cast<uint64_t>(std::numeric_limits<int>::max()))
            throw std::invalid_argument(
                "Environment.contracts.chain_id exceeds the CLOB signer range");
        return environment;
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

    static void configure_transports(HttpClient &clob, HttpClient &data,
                                     const Environment &environment,
                                     const std::optional<HttpClientOptions> &options)
    {
        if (options)
        {
            clob.configure(*options);
            data.configure(*options);
        }
        else
        {
            clob.set_timeout_ms(10000);
            data.set_timeout_ms(10000);
        }
        clob.set_base_url(environment.clob_url);
        data.set_base_url(environment.data_url);
    }

    template <typename Fetch>
    static ClobMarketPage fetch_market_page(Fetch &&fetch, const std::string &endpoint,
                                            const std::string &next_cursor)
    {
        std::string path = endpoint;
        if (!next_cursor.empty())
            path += "?next_cursor=" + percent_encode_query_value(next_cursor);
        const auto response = fetch(path);
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

    ClobClient::ClobClient(Environment environment, const std::string *private_key,
                           const ApiCredentials *creds, SignatureType sig_type,
                           const std::string &funder_address,
                           const std::optional<HttpClientOptions> &http_options)
        : environment_(std::move(environment)),
          funder_address_(private_key ? validated_funder(sig_type, funder_address) : std::string()),
          sig_type_(private_key ? sig_type : SignatureType::EOA)
    {
        configure_transports(http_, data_http_, environment_, http_options);
        if (!private_key) return;
        order_signer_ = std::make_unique<OrderSigner>(
            *private_key, static_cast<int>(environment_.contracts.chain_id));
        if (!creds) return;
        detail::validate_api_credentials(*creds);
        api_creds_ = std::make_unique<ApiCredentials>(*creds);
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id)
        : ClobClient(legacy_environment(base_url, chain_id), nullptr, nullptr, SignatureType::EOA,
                     {}, std::nullopt)
    {
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const HttpClientOptions &http_options)
        : ClobClient(legacy_environment(base_url, chain_id), nullptr, nullptr, SignatureType::EOA,
                     {}, http_options)
    {
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key, SignatureType sig_type,
                           const std::string &funder_address)
        : ClobClient(legacy_environment(base_url, chain_id), &private_key, nullptr, sig_type,
                     funder_address, std::nullopt)
    {
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key, SignatureType sig_type,
                           const std::string &funder_address, const HttpClientOptions &http_options)
        : ClobClient(legacy_environment(base_url, chain_id), &private_key, nullptr, sig_type,
                     funder_address, http_options)
    {
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key, const ApiCredentials &creds,
                           SignatureType sig_type, const std::string &funder_address)
        : ClobClient(legacy_environment(base_url, chain_id), &private_key, &creds, sig_type,
                     funder_address, std::nullopt)
    {
    }

    ClobClient::ClobClient(const std::string &base_url, int chain_id,
                           const std::string &private_key, const ApiCredentials &creds,
                           SignatureType sig_type, const std::string &funder_address,
                           const HttpClientOptions &http_options)
        : ClobClient(legacy_environment(base_url, chain_id), &private_key, &creds, sig_type,
                     funder_address, http_options)
    {
    }

    ClobClient::ClobClient(const Environment &environment,
                           const std::optional<HttpClientOptions> &http_options)
        : ClobClient(validated_environment(environment), nullptr, nullptr, SignatureType::EOA, {},
                     http_options)
    {
    }

    ClobClient::ClobClient(const Environment &environment, const std::string &private_key,
                           SignatureType sig_type, const std::string &funder_address,
                           const std::optional<HttpClientOptions> &http_options)
        : ClobClient(validated_environment(environment), &private_key, nullptr, sig_type,
                     funder_address, http_options)
    {
    }

    ClobClient::ClobClient(const Environment &environment, const std::string &private_key,
                           const ApiCredentials &creds, SignatureType sig_type,
                           const std::string &funder_address,
                           const std::optional<HttpClientOptions> &http_options)
        : ClobClient(validated_environment(environment), &private_key, &creds, sig_type,
                     funder_address, http_options)
    {
    }

    ClobClient::~ClobClient() = default;

    std::string ClobClient::get_exchange_address() const
    {
        return environment_.contracts.standard_exchange;
    }

    std::string ClobClient::get_neg_risk_exchange_address() const
    {
        return environment_.contracts.neg_risk_exchange;
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
        auto response = read([&] { return http_.get("/time"); });
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
        return fetch_market_page([this](const std::string &path)
                                 { return read([&] { return http_.get(path); }); }, "/markets",
                                 next_cursor);
    }

    std::optional<ClobMarket> ClobClient::get_market(const std::string &condition_id)
    {
        if (condition_id.empty()) return std::nullopt;
        auto response = read([&] { return http_.get("/markets/" + condition_id); });
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
        return fetch_market_page([this](const std::string &path)
                                 { return read([&] { return http_.get(path); }); },
                                 "/sampling-markets", next_cursor);
    }

    ClobMarketPage ClobClient::get_simplified_markets(
        const std::string &next_cursor)
    {
        return fetch_market_page([this](const std::string &path)
                                 { return read([&] { return http_.get(path); }); },
                                 "/simplified-markets", next_cursor);
    }

    ClobMarketPage ClobClient::get_sampling_simplified_markets(
        const std::string &next_cursor)
    {
        return fetch_market_page([this](const std::string &path)
                                 { return read([&] { return http_.get(path); }); },
                                 "/sampling-simplified-markets", next_cursor);
    }
}
