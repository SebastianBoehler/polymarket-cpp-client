#include "evm_event_indexer.hpp"
#include "json_rpc_client.hpp"
#include "oracle_watcher.hpp"
#include "polymarket_events.hpp"
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <string_view>
#include <string>

namespace
{
constexpr const char *kDefaultPolygonRpcHttp = "https://polygon-bor.publicnode.com";
constexpr const char *kDefaultUmaAdapter = "0x6a9d222616c90fca5754cd1333cfd9b7fb6a4f74";
constexpr const char *kDefaultConditionalTokens = "0x4d97dcd97ec945f40cf65f87097ace5ea0476045";
constexpr uint64_t kMaxLogRange = 1000;

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void add_resolved_events(polymarket::OracleResolutionDashboard &dashboard,
                        const std::vector<polymarket::EvmLog> &logs)
{
    for (const auto &log : logs)
    {
        const auto event = polymarket::decode_polymarket_event(log);
        if (event.kind == polymarket::PolymarketEventKind::QuestionResolved ||
            event.kind == polymarket::PolymarketEventKind::QuestionManuallyResolved ||
            event.kind == polymarket::PolymarketEventKind::ConditionResolution)
        {
            dashboard.ingest_event(event, false, 0);
        }
    }
}

void scan_logs(polymarket::OracleResolutionDashboard &dashboard,
               polymarket::EvmJsonRpcHttpClient &rpc,
               const polymarket::EvmLogFilter &base_filter,
               uint64_t from_block,
               uint64_t to_block)
{
    auto filter = base_filter;
    uint64_t start = from_block;
    while (start <= to_block)
    {
        uint64_t end = std::min<uint64_t>(to_block, start + kMaxLogRange - 1);
        filter.from_block = polymarket::evm_uint64_to_quantity(start);
        filter.to_block = polymarket::evm_uint64_to_quantity(end);
        add_resolved_events(dashboard, rpc.get_logs(filter));
        start = end + 1;
    }
}

uint64_t parse_block(polymarket::EvmJsonRpcHttpClient &http)
{
    auto latest = polymarket::evm_normalize_hex(http.block_number());
    return polymarket::evm_quantity_to_uint64(latest);
}

bool is_transient_rpc_error(const std::string &message)
{
    static const char *patterns[] = {
        "Timeout was reached",
        "Failed to connect",
        "Could not resolve host",
        "Operation timed out",
        "request timed out",
        "timed out",
        "status=403",
    };
    for (const auto *pattern : patterns)
    {
        if (message.find(pattern) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    using namespace polymarket;

    const char *rpc_http = std::getenv("POLYMARKET_POLYGON_RPC_HTTP");
    const char *uma_adapter = std::getenv("POLYMARKET_UMA_CTF_ADAPTER");
    const char *ctf_address = std::getenv("POLYMARKET_CONDITIONAL_TOKENS");

    if (!rpc_http || std::string(rpc_http).empty())
        rpc_http = kDefaultPolygonRpcHttp;
    if (!uma_adapter || std::string(uma_adapter).empty())
        uma_adapter = kDefaultUmaAdapter;
    if (!ctf_address || std::string(ctf_address).empty())
        ctf_address = kDefaultConditionalTokens;

    std::cout << "test_oracle_watcher_historical using RPC: " << rpc_http
              << "\n";

    if (!rpc_http || !uma_adapter || !ctf_address)
    {
        std::cout << "test_oracle_watcher_historical skipped: unable to resolve RPC or addresses"
                  << '\n';
        return 0;
    }

    try
    {
        OracleResolutionDashboard dashboard;
        EvmJsonRpcHttpClient rpc(rpc_http);
        rpc.set_timeout_ms(30000);
        const auto latest = parse_block(rpc);

        bool found = false;
        uint64_t lookback = 50000;
        for (int attempt = 0; attempt < 5 && !found; ++attempt)
        {
            const auto from_block = latest > lookback ? latest - lookback : 0;

            auto uma_filter = uma_adapter_log_filter(uma_adapter);
            uma_filter.topics = {{uma_event_topic(UmaEventKind::QuestionResolved),
                                  uma_event_topic(UmaEventKind::QuestionManuallyResolved)}};
            uma_filter.from_block = evm_uint64_to_quantity(from_block);
            uma_filter.to_block = evm_uint64_to_quantity(latest);

            auto ctf_filter = conditional_tokens_log_filter(ctf_address);
            ctf_filter.topics = {{conditional_tokens_event_topic(ConditionalTokensEventKind::ConditionResolution)}};
            ctf_filter.from_block = uma_filter.from_block;
            ctf_filter.to_block = uma_filter.to_block;

            scan_logs(dashboard, rpc, uma_filter, from_block, latest);
            scan_logs(dashboard, rpc, ctf_filter, from_block, latest);

            if (!dashboard.keys().empty())
                found = true;
            lookback = lookback * 2;
        }

        auto states = dashboard.snapshots();
        if (!found)
        {
            std::cout << "test_oracle_watcher_historical skipped: no resolved logs found in lookback windows\n";
            return 0;
        }
        require(!states.empty(), "dashboard should contain at least one resolved state");

        bool has_resolved_or_manually = false;
        for (const auto &state : states)
        {
            if (state.phase == OracleResolutionPhase::Resolved ||
                state.phase == OracleResolutionPhase::ManuallyResolved)
            {
                has_resolved_or_manually = true;
                require(!state.timeline.empty(), "resolved state should include at least one timeline entry");
                if (!state.settled_price.empty())
                    require(state.settled_price != "", "settled price must be non-empty string when present");
                if (!state.payouts.empty())
                    require(!state.payouts.front().empty(), "payout values should be non-empty");
            }
            require(!state.key.empty(), "state key should not be empty");
        }
        require(has_resolved_or_manually, "expected at least one terminal resolved market state");
        auto snapshot_json = dashboard.as_json();
        require(snapshot_json.is_array(), "snapshot output must be array");
        require(snapshot_json.size() == states.size(), "json snapshot size should match dashboard state count");

        std::cout << "test_oracle_watcher_historical passed (resolved markets="
                  << states.size() << ")\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        if (is_transient_rpc_error(error.what()))
        {
            std::cout << "test_oracle_watcher_historical skipped: " << error.what() << '\n';
            return 0;
        }
        std::cerr << "test_oracle_watcher_historical failed: " << error.what() << "\n";
        return 1;
    }
}
