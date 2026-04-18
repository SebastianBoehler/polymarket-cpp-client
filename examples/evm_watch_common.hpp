#pragma once

#include "json_rpc_client.hpp"
#include "polymarket_events.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace polymarket_example
{
    inline std::atomic<bool> keep_running{true};

    inline void stop_handler(int)
    {
        keep_running.store(false);
    }

    inline std::string arg_value(int argc, char **argv, const std::string &name)
    {
        const auto prefix = name + "=";
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == name && i + 1 < argc)
                return argv[i + 1];
            if (arg.rfind(prefix, 0) == 0)
                return arg.substr(prefix.size());
        }
        return "";
    }

    inline bool has_flag(int argc, char **argv, const std::string &name)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (argv[i] == name)
                return true;
        }
        return false;
    }

    inline std::string config_value(int argc, char **argv,
                                    const std::string &arg_name,
                                    const std::string &env_name)
    {
        auto value = arg_value(argc, argv, arg_name);
        if (!value.empty())
            return value;
        const char *env = std::getenv(env_name.c_str());
        return env ? std::string(env) : "";
    }

    inline nlohmann::json event_json(const polymarket::PolymarketDecodedEvent &event)
    {
        nlohmann::json out = {
            {"name", event.name},
            {"address", event.raw_log.address},
            {"blockNumber", event.raw_log.block_number},
            {"transactionHash", event.raw_log.transaction_hash},
            {"removed", event.raw_log.removed}};

        auto put = [&out](const char *key, const std::string &value)
        {
            if (!value.empty())
                out[key] = value;
        };
        put("questionId", event.question_id);
        put("conditionId", event.condition_id);
        put("requestTimestamp", event.request_timestamp);
        put("creator", event.creator);
        put("rewardToken", event.reward_token);
        put("reward", event.reward);
        put("proposalBond", event.proposal_bond);
        put("oracle", event.oracle);
        put("settledPrice", event.settled_price);
        put("outcomeSlotCount", event.outcome_slot_count);
        put("redeemer", event.redeemer);
        put("collateralToken", event.collateral_token);
        put("parentCollectionId", event.parent_collection_id);
        put("payout", event.payout);
        if (!event.payouts.empty())
            out["payouts"] = event.payouts;
        if (!event.index_sets.empty())
            out["indexSets"] = event.index_sets;
        return out;
    }

    inline void print_usage(const char *program, const std::string &address_arg,
                            const std::string &address_env)
    {
        std::cerr << "Usage: " << program << " --rpc-ws <wss-url> "
                  << address_arg << " <address> [--rpc-http <https-url> "
                  << "--from-block <hex|number>] [--to-block <hex|latest>] "
                  << "[--pending] [--heads] [--duration-seconds <n>] "
                  << "[--timeout-ms <n>]\n"
                  << "Env: POLYMARKET_POLYGON_RPC_WS, POLYMARKET_POLYGON_RPC_HTTP, "
                  << address_env << "\n";
    }

    inline int run_watch(int argc, char **argv,
                         const std::string &address_arg,
                         const std::string &address_env,
                         const std::function<polymarket::EvmLogFilter(const std::string &)> &make_filter)
    {
        if (has_flag(argc, argv, "--help"))
        {
            print_usage(argv[0], address_arg, address_env);
            return 0;
        }

        auto rpc_ws = config_value(argc, argv, "--rpc-ws", "POLYMARKET_POLYGON_RPC_WS");
        auto rpc_http = config_value(argc, argv, "--rpc-http", "POLYMARKET_POLYGON_RPC_HTTP");
        auto address = config_value(argc, argv, address_arg, address_env);
        auto from_block = arg_value(argc, argv, "--from-block");
        auto to_block = arg_value(argc, argv, "--to-block");
        auto duration = arg_value(argc, argv, "--duration-seconds");
        auto timeout_ms = arg_value(argc, argv, "--timeout-ms");
        long http_timeout_ms = 0;

        if (rpc_ws.empty() || address.empty())
        {
            print_usage(argv[0], address_arg, address_env);
            return 1;
        }

        auto filter = make_filter(address);
        if (!from_block.empty())
        {
            if (rpc_http.empty())
            {
                std::cerr << "--from-block requires --rpc-http or POLYMARKET_POLYGON_RPC_HTTP\n";
                return 1;
            }
            filter.from_block = from_block;
            filter.to_block = to_block.empty() ? "latest" : to_block;
            try
            {
                polymarket::EvmJsonRpcHttpClient http(rpc_http);
                if (!timeout_ms.empty())
                {
                    http_timeout_ms = std::stol(timeout_ms);
                    if (http_timeout_ms <= 0)
                        throw std::invalid_argument("--timeout-ms must be positive");
                    http.set_timeout_ms(http_timeout_ms);
                }
                for (const auto &log : http.get_logs(filter))
                {
                    std::cout << event_json(polymarket::decode_polymarket_event(log)).dump() << "\n";
                }
            }
            catch (const std::exception &error)
            {
                std::cerr << "[rpc] catch-up failed: " << error.what() << "\n";
                return 1;
            }
            filter.from_block.clear();
            filter.to_block.clear();
        }

        std::signal(SIGINT, stop_handler);
        std::signal(SIGTERM, stop_handler);

        polymarket::EvmJsonRpcWsClient ws(rpc_ws);
        ws.on_error([](const std::string &error)
                    { std::cerr << "[rpc] " << error << "\n"; });
        ws.on_log([](const polymarket::EvmLog &log)
                  {
            try
            {
                std::cout << event_json(polymarket::decode_polymarket_event(log)).dump() << "\n";
            }
            catch (const std::exception &error)
            {
                std::cerr << "[decode] " << error.what() << "\n";
            } });
        ws.on_pending_transaction([](const std::string &tx_hash)
                                  { std::cout << nlohmann::json({{"type", "pending_transaction"},
                                                                  {"txHash", tx_hash}})
                                                   .dump()
                                              << "\n"; });
        ws.on_head([](const nlohmann::json &head)
                   { std::cout << nlohmann::json({{"type", "head"}, {"head", head}}).dump() << "\n"; });

        ws.subscribe_logs(filter);
        if (has_flag(argc, argv, "--pending"))
            ws.subscribe_pending_transactions();
        if (has_flag(argc, argv, "--heads"))
            ws.subscribe_new_heads();
        if (!ws.connect())
            return 1;

        const int seconds = duration.empty() ? 0 : std::atoi(duration.c_str());
        for (int elapsed = 0; keep_running.load(); ++elapsed)
        {
            if (seconds > 0 && elapsed >= seconds)
                break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        ws.stop();
        return 0;
    }

} // namespace polymarket_example
