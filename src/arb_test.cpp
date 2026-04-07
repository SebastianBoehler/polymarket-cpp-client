#include "arb/client_bootstrap.hpp"
#include "arb/execution_runner.hpp"
#include "arb/market_runtime.hpp"
#include "arb/polymarket_execution_adapter.hpp"
#include "arb/signal_detector.hpp"
#include "market_fetcher.hpp"
#include <cstdlib>
#include <iostream>

using namespace polymarket;
using namespace polymarket::arb;

namespace
{
    void print_usage()
    {
        std::cout << "Usage: arb_test [--help]\n"
                  << "Uses the new arb engine modules to execute a single BTC 15m snapshot check.\n"
                  << std::endl;
    }
} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--help")
        {
            print_usage();
            return 0;
        }
    }

    http_global_init();

    ArbConfig arb_config;
    if (const char *size_env = std::getenv("SIZE_USDC"))
        arb_config.per_leg_size_usdc = std::stod(size_env);
    if (const char *trigger_env = std::getenv("TRIGGER_COMBINED"))
        arb_config.trigger_threshold = std::stod(trigger_env);
    const bool dry_run = std::getenv("DRY_RUN")
                             ? std::string(std::getenv("DRY_RUN")) != "false"
                             : true;

    Config runtime_config;
    MarketFetcher fetcher(runtime_config);
    MarketDiscoveryOptions discovery;
    auto markets = fetch_markets(fetcher, discovery);
    auto *selected = select_best_market(markets, "btc", "15m");
    if (!selected)
    {
        std::cerr << "[arb_test] No BTC 15m market available." << std::endl;
        http_global_cleanup();
        return 1;
    }

    ClobClient public_client(runtime_config.clob_rest_url, 137);
    const auto prepared = prepare_market(public_client, *selected, "15m");
    if (!prepared)
    {
        std::cerr << "[arb_test] Failed to fetch market metadata." << std::endl;
        http_global_cleanup();
        return 1;
    }

    const auto yes_book = fetcher.fetch_orderbook(prepared->market.token_yes);
    const auto no_book = fetcher.fetch_orderbook(prepared->market.token_no);
    if (!yes_book || !no_book)
    {
        std::cerr << "[arb_test] Failed to fetch both orderbooks." << std::endl;
        http_global_cleanup();
        return 1;
    }

    SignalDetector detector(arb_config);
    const auto signal = detector.detect(*prepared, *yes_book, *no_book);
    if (!signal)
    {
        std::cout << "[arb_test] No qualifying opportunity in current snapshot." << std::endl;
        http_global_cleanup();
        return 0;
    }

    std::shared_ptr<ClobClient> live_client;
    if (!dry_run)
    {
        const char *private_key = std::getenv("PRIVATE_KEY");
        if (!private_key)
        {
            std::cerr << "[arb_test] PRIVATE_KEY is required for live mode." << std::endl;
            http_global_cleanup();
            return 1;
        }
        const char *funder = std::getenv("FUNDER_ADDRESS");
        auto session = create_authenticated_session(
            runtime_config.clob_rest_url,
            137,
            private_key,
            funder ? funder : "",
            api_credentials_from_env());
        live_client = session.client;
        live_client->warm_connection();
    }

    auto adapter = PolymarketExecutionAdapter(live_client, dry_run);
    RiskPolicy risk_policy(arb_config);
    const auto gate = risk_policy.allow(*signal);
    if (!gate.allowed)
    {
        std::cout << "[arb_test] Blocked by risk policy: " << gate.reason << std::endl;
        http_global_cleanup();
        return 0;
    }

    ExecutionRunner runner(arb_config, adapter, risk_policy);
    const auto report = runner.execute(*signal, *yes_book, *no_book);

    std::cout << "[arb_test] signal=" << report.signal_id
              << " both_filled=" << report.both_filled
              << " one_filled=" << report.one_filled
              << " both_rejected=" << report.both_rejected
              << " dry_run=" << report.dry_run << '\n'
              << "  YES status=" << report.yes_leg.status
              << " error=" << report.yes_leg.error_msg << '\n'
              << "  NO  status=" << report.no_leg.status
              << " error=" << report.no_leg.error_msg << '\n';
    if (report.flatten_report)
    {
        std::cout << "  flatten success=" << report.flatten_report->success
                  << " error=" << report.flatten_report->error_msg << '\n';
    }

    http_global_cleanup();
    return 0;
}
