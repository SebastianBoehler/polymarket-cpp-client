#pragma once

#include "clob_client.hpp"
#include "types.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace polymarket::arb
{

    struct ArbConfig
    {
        double trigger_threshold = 0.98;
        double per_leg_size_usdc = 5.0;
        double slippage_buffer = 0.005;
        double fee_buffer_bps = 0.0;
        uint64_t max_book_age_ms = 1500;
        double minimum_visible_depth_ratio = 1.0;
        uint64_t orphan_unwind_timeout_ms = 1500;
        uint64_t cooldown_ms = 2500;
    };

    struct MarketMetadata
    {
        double tick_size = 0.01;
        bool neg_risk = false;
    };

    struct PreparedMarket
    {
        MarketState market;
        MarketMetadata metadata;
        uint64_t expiry_ms = 0;
    };

    struct OpportunitySignal
    {
        PreparedMarket prepared_market;
        uint64_t signal_detected_at_ns = 0;
        uint64_t yes_book_timestamp_ns = 0;
        uint64_t no_book_timestamp_ns = 0;
        uint64_t snapshot_key = 0;
        double raw_best_ask_yes = 0.0;
        double raw_best_ask_no = 0.0;
        double adjusted_yes_price = 0.0;
        double adjusted_no_price = 0.0;
        double effective_combined = 0.0;
        double visible_depth_ratio_yes = 0.0;
        double visible_depth_ratio_no = 0.0;
    };

    struct LegPlan
    {
        std::string label;
        std::string token_id;
        double limit_price = 0.0;
        double size_shares = 0.0;
        double spend_usdc = 0.0;
        CreateOrderParams order_params;
    };

    struct ExecutionPlan
    {
        OpportunitySignal signal;
        uint64_t plan_ready_at_ns = 0;
        LegPlan yes_leg;
        LegPlan no_leg;
        double total_spend_usdc = 0.0;
        double guaranteed_payout_usdc = 0.0;
        double expected_profit_usdc = 0.0;
        double worst_case_orphan_exposure_usdc = 0.0;
    };

    struct LegReport
    {
        std::string label;
        std::string token_id;
        std::string status;
        std::string order_id;
        std::string error_msg;
        bool submitted = false;
        bool filled = false;
        double requested_limit_price = 0.0;
        double requested_shares = 0.0;
        double requested_spend_usdc = 0.0;
        double filled_shares = 0.0;
        double spent_usdc = 0.0;
    };

    struct SubmitResult
    {
        bool attempted = false;
        bool success = false;
        bool dry_run = false;
        uint64_t signed_at_ns = 0;
        uint64_t submitted_at_ns = 0;
        uint64_t response_at_ns = 0;
        std::string error_msg;
        LegReport yes_leg;
        LegReport no_leg;
    };

    struct FlattenRequest
    {
        std::string token_id;
        double shares = 0.0;
        double limit_price = 0.0;
        uint64_t deadline_ns = 0;
        std::string reason;
    };

    struct FlattenReport
    {
        bool attempted = false;
        bool success = false;
        uint64_t started_at_ns = 0;
        uint64_t completed_at_ns = 0;
        std::string token_id;
        std::string order_id;
        std::string status;
        std::string error_msg;
        double shares_requested = 0.0;
        double shares_closed = 0.0;
    };

    struct ExecutionReport
    {
        std::string venue = "polymarket";
        std::string signal_id;
        bool dry_run = false;
        bool plan_created = false;
        bool submit_attempted = false;
        bool submit_succeeded = false;
        bool both_filled = false;
        bool one_filled = false;
        bool both_rejected = false;
        uint64_t signal_detected_at_ns = 0;
        uint64_t plan_ready_at_ns = 0;
        uint64_t signed_at_ns = 0;
        uint64_t submitted_at_ns = 0;
        uint64_t response_at_ns = 0;
        int orphan_fill_count = 0;
        std::string error_msg;
        std::string cooldown_reason;
        LegReport yes_leg;
        LegReport no_leg;
        std::optional<FlattenReport> flatten_report;
    };

    struct GateDecision
    {
        bool allowed = false;
        std::string reason;
    };

    struct ExecutionCounters
    {
        uint64_t opportunities_seen = 0;
        uint64_t opportunities_skipped = 0;
        uint64_t orders_submitted = 0;
        uint64_t both_filled = 0;
        uint64_t one_filled = 0;
        uint64_t both_rejected = 0;
    };

} // namespace polymarket::arb
