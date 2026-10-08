#include "polymarket/clob_client.hpp"
#include "clob_client_internal.hpp"
#include "poll_deadline.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace polymarket
{
    namespace
    {
        constexpr const char *trades_endpoint = "/data/trades";

        // CONFIRMED is final on-chain; FAILED never will be. Earlier statuses
        // (MATCHED, MINED, RETRYING) can still change their transaction hash.
        bool is_settled(const Trade &trade)
        {
            return trade.status == "CONFIRMED" || trade.status == "FAILED";
        }

        bool is_failed(const Trade &trade)
        {
            return trade.status == "FAILED";
        }

        void append_unique(std::vector<std::string> &values, const std::string &value)
        {
            if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end())
                values.push_back(value);
        }

        std::string join(const std::vector<std::string> &values)
        {
            std::string result;
            for (const auto &value : values)
                result += (result.empty() ? "" : ", ") + value;
            return result;
        }

        SdkError settlement_error(SdkErrorCode code, const std::string &message,
                                  bool retryable = false)
        {
            return {code, message, trades_endpoint, 0, "", "", retryable};
        }
    } // namespace

    std::optional<Trade> ClobClient::get_trade(const std::string &trade_id)
    {
        auto result = get_trade_result(trade_id);
        return result ? result.value() : std::nullopt;
    }

    Result<std::optional<Trade>> ClobClient::get_trade_result(const std::string &trade_id)
    {
        if (!order_signer_ || !api_creds_)
            return Result<std::optional<Trade>>::failure(
                make_auth_error("Client not authenticated", trades_endpoint));
        if (trade_id.empty())
            return Result<std::optional<Trade>>::failure(
                settlement_error(SdkErrorCode::InvalidArgument, "trade_id is required"));

        const std::string path =
            std::string(trades_endpoint) + "?id=" + detail::percent_encode_query_value(trade_id);
        auto response =
            read([&] { return http_.get(path, get_l2_headers("GET", trades_endpoint, "")); });
        if (!response.ok())
            return Result<std::optional<Trade>>::failure(make_sdk_error(response, trades_endpoint));

        try
        {
            auto page = detail::parse_trade_page_json(response.body);
            for (auto &trade : page.trades)
            {
                if (trade.id == trade_id)
                    return Result<std::optional<Trade>>::success(std::move(trade));
            }
            return Result<std::optional<Trade>>::success(std::nullopt);
        }
        catch (const std::exception &ex)
        {
            return Result<std::optional<Trade>>::failure(
                make_parse_error(ex.what(), trades_endpoint, response.body));
        }
    }

    Result<OrderSettlement>
    ClobClient::wait_for_order_fill_settlement(const OrderResponse &order,
                                               std::chrono::milliseconds timeout,
                                               std::chrono::milliseconds poll_interval)
    {
        if (timeout < std::chrono::milliseconds::zero() ||
            poll_interval <= std::chrono::milliseconds::zero())
            return Result<OrderSettlement>::failure(settlement_error(
                SdkErrorCode::InvalidArgument,
                "timeout must not be negative and poll_interval must be positive"));

        std::vector<std::string> trade_ids;
        for (const auto &trade_id : order.trade_ids)
            append_unique(trade_ids, trade_id);

        OrderSettlement settlement;
        if (trade_ids.empty())
        {
            // Nothing matched on arrival, so there are no fills to poll.
            for (const auto &hash : order.transaction_hashes)
                append_unique(settlement.transaction_hashes, hash);
            return Result<OrderSettlement>::success(std::move(settlement));
        }

        std::vector<std::optional<Trade>> settled(trade_ids.size());
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true)
        {
            std::vector<std::string> pending;
            for (std::size_t index = 0; index < trade_ids.size(); ++index)
            {
                if (settled[index]) continue;
                auto trade = get_trade_result(trade_ids[index]);
                if (!trade) return Result<OrderSettlement>::failure(trade.error());
                if (trade.value() && is_settled(*trade.value()))
                    settled[index] = std::move(trade.value());
                else
                    pending.push_back(trade_ids[index]);
            }
            if (pending.empty()) break;
            if (!detail::sleep_before_next_poll(deadline, poll_interval))
                return Result<OrderSettlement>::failure(settlement_error(
                    SdkErrorCode::Timeout,
                    "timed out waiting for trades to settle: " + join(pending), true));
        }

        bool all_failed = true;
        settlement.trades.reserve(settled.size());
        for (auto &trade : settled)
        {
            if (!is_failed(*trade))
            {
                all_failed = false;
                append_unique(settlement.transaction_hashes, trade->transaction_hash);
            }
            settlement.trades.push_back(std::move(*trade));
        }
        if (all_failed)
            return Result<OrderSettlement>::failure(settlement_error(
                SdkErrorCode::TransactionFailed,
                "every fill of order " + order.order_id + " failed execution: " + join(trade_ids)));
        return Result<OrderSettlement>::success(std::move(settlement));
    }
} // namespace polymarket
