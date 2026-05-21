#include "sdk_error.hpp"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace polymarket
{
    namespace
    {
        std::string excerpt(const std::string &body)
        {
            constexpr std::size_t kMaxExcerpt = 512;
            if (body.size() <= kMaxExcerpt)
            {
                return body;
            }
            return body.substr(0, kMaxExcerpt);
        }

        std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
            return value;
        }

        std::string header_value(const HttpResponse &response, const std::string &key)
        {
            const auto it = response.headers.find(lower(key));
            return it == response.headers.end() ? "" : it->second;
        }

        std::string parse_api_error_message(const std::string &body)
        {
            if (body.empty())
            {
                return "";
            }

            try
            {
                const auto parsed = json::parse(body);
                if (parsed.is_string())
                {
                    return parsed.get<std::string>();
                }
                for (const auto *key : {"error", "errorMsg", "message", "detail"})
                {
                    if (parsed.contains(key))
                    {
                        const auto &value = parsed[key];
                        if (value.is_string())
                        {
                            return value.get<std::string>();
                        }
                        return value.dump();
                    }
                }
            }
            catch (...)
            {
            }

            return "";
        }

        bool is_deposit_wallet_signer_mismatch(const std::string &endpoint,
                                               long status_code,
                                               const std::string &message)
        {
            const auto normalized = lower(message);
            return endpoint == "/order" &&
                   status_code == 400 &&
                   normalized.find("order signer address") != std::string::npos &&
                   normalized.find("address of the api key") != std::string::npos;
        }

        std::string deposit_wallet_setup_message()
        {
            return "POLY_1271 deposit wallet order was rejected because the order signer "
                   "does not match the CLOB API key address. Check that the funder is the "
                   "deployed deposit wallet/proxy address, the wallet contract has bytecode "
                   "on Polygon, approvals and pUSD balance are set on that wallet, and "
                   "update_balance_allowance was called with signature_type=3 before posting.";
        }
    }

    SdkError make_sdk_error(const HttpResponse &response, const std::string &endpoint)
    {
        SdkError error;
        error.endpoint = endpoint;
        error.http_status = response.status_code;
        error.response_body_excerpt = excerpt(response.body);
        error.request_id = header_value(response, "x-request-id");
        if (error.request_id.empty())
        {
            error.request_id = header_value(response, "cf-ray");
        }

        if (!response.error.empty())
        {
            error.code = SdkErrorCode::HttpTransport;
            error.message = response.error;
            error.retryable = true;
            return error;
        }

        const auto api_message = parse_api_error_message(response.body);
        error.message = api_message.empty() ? "API request failed" : api_message;

        if (is_deposit_wallet_signer_mismatch(endpoint, response.status_code, error.message))
        {
            error.code = SdkErrorCode::DepositWalletSetup;
            error.message = deposit_wallet_setup_message();
            error.retryable = false;
            return error;
        }

        if (response.status_code == 401 || response.status_code == 403)
        {
            error.code = SdkErrorCode::Auth;
            error.retryable = false;
        }
        else if (response.status_code == 429)
        {
            error.code = SdkErrorCode::RateLimit;
            error.retryable = true;
        }
        else if (response.status_code >= 500)
        {
            error.code = SdkErrorCode::ApiResponse;
            error.retryable = true;
        }
        else
        {
            error.code = SdkErrorCode::ApiResponse;
            error.retryable = false;
        }

        return error;
    }

    SdkError make_auth_error(const std::string &message, const std::string &endpoint)
    {
        return {SdkErrorCode::Auth, message, endpoint, 0, "", "", false};
    }

    SdkError make_parse_error(const std::string &message, const std::string &endpoint, const std::string &body)
    {
        return {SdkErrorCode::Parse, message, endpoint, 0, excerpt(body), "", false};
    }

    std::string sdk_error_code_to_string(SdkErrorCode code)
    {
        switch (code)
        {
        case SdkErrorCode::HttpTransport:
            return "http_transport";
        case SdkErrorCode::ApiResponse:
            return "api_response";
        case SdkErrorCode::Auth:
            return "auth";
        case SdkErrorCode::DepositWalletSetup:
            return "deposit_wallet_setup";
        case SdkErrorCode::RateLimit:
            return "rate_limit";
        case SdkErrorCode::Parse:
            return "parse";
        case SdkErrorCode::Signing:
            return "signing";
        case SdkErrorCode::InvalidArgument:
            return "invalid_argument";
        }
        return "unknown";
    }
}
