#include "arb/polymarket_execution_adapter.hpp"
#include <optional>

namespace polymarket::arb
{

    namespace
    {
        double parse_amount(const std::string &value)
        {
            try
            {
                return std::stod(value);
            }
            catch (...)
            {
                return 0.0;
            }
        }

        LegReport make_planned_leg(const LegPlan &plan)
        {
            LegReport report;
            report.label = plan.label;
            report.token_id = plan.token_id;
            report.requested_limit_price = plan.limit_price;
            report.requested_shares = plan.size_shares;
            report.requested_spend_usdc = plan.spend_usdc;
            return report;
        }

        LegReport map_response(const LegPlan &plan,
                               const std::optional<OrderResponse> &response)
        {
            LegReport report = make_planned_leg(plan);
            if (!response.has_value())
            {
                report.error_msg = "missing_batch_leg_response";
                return report;
            }

            const auto &item = *response;
            report.submitted = item.success || !item.error_msg.empty() ||
                               !item.status.empty() || !item.order_id.empty();
            report.status = item.status;
            report.order_id = item.order_id;
            report.error_msg = item.error_msg;

            const double taking_amount = parse_amount(item.taking_amount);
            const double making_amount = parse_amount(item.making_amount);
            report.filled = item.success && item.error_msg.empty() &&
                            (taking_amount > 0.0 || making_amount > 0.0 ||
                             item.status == "matched");
            report.filled_shares =
                taking_amount > 0.0 ? taking_amount : (report.filled ? plan.size_shares : 0.0);
            report.spent_usdc =
                making_amount > 0.0 ? making_amount : (report.filled ? plan.spend_usdc : 0.0);
            return report;
        }
    } // namespace

    PolymarketExecutionAdapter::PolymarketExecutionAdapter(
        std::shared_ptr<ClobClient> client,
        bool dry_run)
        : client_(std::move(client)), dry_run_(dry_run)
    {
    }

    bool PolymarketExecutionAdapter::live_trading_enabled() const
    {
        return !dry_run_ && client_ != nullptr;
    }

    SubmitResult PolymarketExecutionAdapter::submit(const ExecutionPlan &plan)
    {
        SubmitResult result;
        result.dry_run = dry_run_;
        result.yes_leg = make_planned_leg(plan.yes_leg);
        result.no_leg = make_planned_leg(plan.no_leg);

        if (dry_run_)
        {
            const uint64_t timestamp = now_ns();
            result.signed_at_ns = timestamp;
            result.submitted_at_ns = timestamp;
            result.response_at_ns = timestamp;
            return result;
        }

        if (!client_)
        {
            result.error_msg = "missing_execution_client";
            return result;
        }

        try
        {
            const auto signed_yes = client_->create_order(plan.yes_leg.order_params);
            const auto signed_no = client_->create_order(plan.no_leg.order_params);
            result.signed_at_ns = now_ns();
            result.attempted = true;

            std::vector<BatchOrderEntry> batch = {
                {signed_yes, OrderType::FOK},
                {signed_no, OrderType::FOK}};
            result.submitted_at_ns = now_ns();
            const auto responses = client_->post_orders(batch);
            result.response_at_ns = now_ns();
            result.success = true;

            if (responses.size() >= 2)
            {
                result.yes_leg = map_response(plan.yes_leg, responses[0]);
                result.no_leg = map_response(plan.no_leg, responses[1]);
                return result;
            }

            if (responses.size() == 1)
            {
                result.yes_leg = map_response(plan.yes_leg, responses[0]);
                result.no_leg = map_response(plan.no_leg, responses[0]);
                return result;
            }

            result.error_msg = "empty_batch_response";
            result.yes_leg.error_msg = result.error_msg;
            result.no_leg.error_msg = result.error_msg;
            return result;
        }
        catch (const std::exception &e)
        {
            result.error_msg = e.what();
            result.response_at_ns = now_ns();
            return result;
        }
    }

    FlattenReport PolymarketExecutionAdapter::flatten(const FlattenRequest &request,
                                                      const PreparedMarket &market)
    {
        FlattenReport report;
        report.token_id = request.token_id;
        report.shares_requested = request.shares;
        report.started_at_ns = now_ns();

        if (dry_run_)
        {
            report.completed_at_ns = report.started_at_ns;
            report.status = "dry_run";
            return report;
        }

        if (!client_)
        {
            report.completed_at_ns = now_ns();
            report.error_msg = "missing_execution_client";
            return report;
        }

        if (report.started_at_ns > request.deadline_ns)
        {
            report.completed_at_ns = now_ns();
            report.error_msg = "flatten_deadline_expired";
            return report;
        }

        try
        {
            CreateOrderParams params;
            params.token_id = request.token_id;
            params.price = request.limit_price;
            params.size = request.shares;
            params.side = OrderSide::SELL;
            params.neg_risk = market.metadata.neg_risk;

            const auto signed_order = client_->create_order(params);
            const auto response = client_->post_order(signed_order, OrderType::FOK);
            report.completed_at_ns = now_ns();
            report.attempted = true;
            report.order_id = response.order_id;
            report.status = response.status;
            report.error_msg = response.error_msg;
            report.success = response.success && response.error_msg.empty() &&
                             (!response.order_id.empty() || response.status == "matched");
            report.shares_closed = report.success ? request.shares : 0.0;
            return report;
        }
        catch (const std::exception &e)
        {
            report.completed_at_ns = now_ns();
            report.attempted = true;
            report.error_msg = e.what();
            return report;
        }
    }

} // namespace polymarket::arb
