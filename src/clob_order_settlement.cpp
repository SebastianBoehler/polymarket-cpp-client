#include "polymarket/clob_client.hpp"
#include "clob_client_internal.hpp"
#include "poll_deadline.hpp"
#include "polymarket/trade_status_tracker.hpp"
#include "trade_status.hpp"

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

        bool has_settlement_details(const Trade &trade)
        {
            const auto status = detail::normalized_trade_status(trade.status);
            return status == "FAILED" || (status == "CONFIRMED" && !trade.transaction_hash.empty());
        }

        bool is_failed(const Trade &trade)
        {
            return detail::is_failed_trade_status(trade.status);
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

        std::vector<std::string> unique_trade_ids(const OrderResponse &order)
        {
            std::vector<std::string> trade_ids;
            for (const auto &trade_id : order.trade_ids)
                append_unique(trade_ids, trade_id);
            return trade_ids;
        }

        OrderSettlement unfilled_settlement(const OrderResponse &order)
        {
            OrderSettlement settlement;
            for (const auto &hash : order.transaction_hashes)
                append_unique(settlement.transaction_hashes, hash);
            return settlement;
        }

        std::vector<std::string> pending_ids(const std::vector<std::string> &trade_ids,
                                             const std::vector<std::optional<Trade>> &settled)
        {
            std::vector<std::string> pending;
            for (std::size_t index = 0; index < trade_ids.size(); ++index)
                if (!settled[index]) pending.push_back(trade_ids[index]);
            return pending;
        }

        Result<OrderSettlement> finish_settlement(const OrderResponse &order,
                                                  const std::vector<std::string> &trade_ids,
                                                  std::vector<std::optional<Trade>> &settled)
        {
            OrderSettlement settlement;
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
                    SdkErrorCode::TransactionFailed, "every fill of order " + order.order_id +
                                                         " failed execution: " + join(trade_ids)));
            return Result<OrderSettlement>::success(std::move(settlement));
        }
    } // namespace

    std::optional<Trade> ClobClient::get_trade(const std::string &trade_id)
    {
        auto result = get_trade_result(trade_id);
        return result ? result.value() : std::nullopt;
    }

    Result<std::optional<Trade>> ClobClient::get_trade_result(const std::string &trade_id)
    {
        return lookup_trade(trade_id, std::nullopt);
    }

    Result<std::optional<Trade>>
    ClobClient::lookup_trade(const std::string &trade_id,
                             std::optional<std::chrono::steady_clock::time_point> deadline)
    {
        if (!order_signer_ || !api_creds_)
            return Result<std::optional<Trade>>::failure(
                make_auth_error("Client not authenticated", trades_endpoint));
        if (trade_id.empty())
            return Result<std::optional<Trade>>::failure(
                settlement_error(SdkErrorCode::InvalidArgument, "trade_id is required"));

        const std::string path =
            std::string(trades_endpoint) + "?id=" + detail::percent_encode_query_value(trade_id);
        auto response = read(
            [&] { return http_.get(path, get_l2_headers("GET", trades_endpoint, "")); }, deadline);
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

        const auto trade_ids = unique_trade_ids(order);
        // Nothing matched on arrival, so there are no fills to poll.
        if (trade_ids.empty()) return Result<OrderSettlement>::success(unfilled_settlement(order));

        std::vector<std::optional<Trade>> settled(trade_ids.size());
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true)
        {
            std::vector<std::string> pending;
            for (std::size_t index = 0; index < trade_ids.size(); ++index)
            {
                if (settled[index]) continue;
                auto trade = lookup_trade(trade_ids[index], deadline);
                if (!trade) return Result<OrderSettlement>::failure(trade.error());
                if (trade.value() && has_settlement_details(*trade.value()))
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

        return finish_settlement(order, trade_ids, settled);
    }

    Result<OrderSettlement> ClobClient::wait_for_order_fill_settlement(
        const OrderResponse &order, const TradeStatusTracker &tracker,
        std::chrono::milliseconds timeout, std::chrono::milliseconds reconcile_interval)
    {
        if (timeout < std::chrono::milliseconds::zero() ||
            reconcile_interval <= std::chrono::milliseconds::zero())
            return Result<OrderSettlement>::failure(settlement_error(
                SdkErrorCode::InvalidArgument,
                "timeout must not be negative and reconcile_interval must be positive"));

        const auto trade_ids = unique_trade_ids(order);
        if (trade_ids.empty()) return Result<OrderSettlement>::success(unfilled_settlement(order));

        std::vector<std::optional<Trade>> settled(trade_ids.size());
        std::vector<bool> hash_lookup_requested(trade_ids.size(), false);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        auto next_reconcile = std::chrono::steady_clock::now() + reconcile_interval;
        auto reconciled_recoveries = tracker.recoveries();
        while (true)
        {
            const auto seen_version = tracker.version();
            bool needs_hash_lookup = false;
            for (std::size_t index = 0; index < trade_ids.size(); ++index)
            {
                if (settled[index]) continue;
                auto trade = tracker.latest(trade_ids[index]);
                if (trade && has_settlement_details(*trade))
                    settled[index] = std::move(trade);
                else if (trade && detail::is_settled_trade_status(trade->status) &&
                         !hash_lookup_requested[index])
                {
                    hash_lookup_requested[index] = true;
                    needs_hash_lookup = true;
                }
            }
            auto pending = pending_ids(trade_ids, settled);
            if (pending.empty()) break;

            const auto now = std::chrono::steady_clock::now();
            const auto recoveries = tracker.recoveries();
            if (needs_hash_lookup || now >= next_reconcile || now >= deadline ||
                recoveries != reconciled_recoveries)
            {
                reconciled_recoveries = recoveries;
                for (std::size_t index = 0; index < trade_ids.size(); ++index)
                {
                    if (settled[index]) continue;
                    auto trade = lookup_trade(trade_ids[index], deadline);
                    if (!trade) return Result<OrderSettlement>::failure(trade.error());
                    if (trade.value() && has_settlement_details(*trade.value()))
                        settled[index] = std::move(trade.value());
                }
                pending = pending_ids(trade_ids, settled);
                if (pending.empty()) break;
                if (std::chrono::steady_clock::now() >= deadline)
                    return Result<OrderSettlement>::failure(settlement_error(
                        SdkErrorCode::Timeout,
                        "timed out waiting for trades to settle: " + join(pending), true));
                next_reconcile = std::chrono::steady_clock::now() + reconcile_interval;
            }
            tracker.wait_for_change(seen_version, std::min(deadline, next_reconcile));
        }
        return finish_settlement(order, trade_ids, settled);
    }
} // namespace polymarket
