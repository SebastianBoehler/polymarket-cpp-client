#include "evm_event_indexer.hpp"
#include "polymarket_events.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
std::string arg_value(int argc, char **argv, const std::string &name)
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

bool has_flag(int argc, char **argv, const std::string &name)
{
    for (int i = 1; i < argc; ++i)
        if (argv[i] == name)
            return true;
    return false;
}

std::string env_or_arg(int argc, char **argv, const std::string &arg, const std::string &env)
{
    auto value = arg_value(argc, argv, arg);
    if (!value.empty())
        return value;
    const char *from_env = std::getenv(env.c_str());
    return from_env ? std::string(from_env) : "";
}

nlohmann::json event_json(const polymarket::EvmIndexedLog &indexed)
{
    auto decoded = polymarket::decode_polymarket_event(indexed.log);
    nlohmann::json out = {
        {"source", indexed.live ? "live" : "catchup"},
        {"name", decoded.name},
        {"address", indexed.log.address},
        {"blockNumber", indexed.log.block_number},
        {"transactionHash", indexed.log.transaction_hash},
        {"removed", indexed.log.removed}};
    if (!decoded.condition_id.empty())
        out["conditionId"] = decoded.condition_id;
    if (!decoded.question_id.empty())
        out["questionId"] = decoded.question_id;
    if (!decoded.payouts.empty())
        out["payouts"] = decoded.payouts;
    if (!decoded.index_sets.empty())
        out["indexSets"] = decoded.index_sets;
    return out;
}

void print_usage(const char *program)
{
    std::cerr << "Usage: " << program
              << " --rpc-http <url> (--ctf <addr>|--uma-adapter <addr>) "
              << "[--rpc-ws <url> --live] [--cursor-file <path>] "
              << "[--cursor-name <name>] [--start-block <hex|number>] "
              << "[--batch-size <n>] [--confirmations <n>] "
              << "[--duration-seconds <n>]\n"
              << "Env: POLYMARKET_POLYGON_RPC_HTTP, POLYMARKET_POLYGON_RPC_WS, "
              << "POLYMARKET_CONDITIONAL_TOKENS, POLYMARKET_UMA_CTF_ADAPTER, "
              << "POLYMARKET_INDEXER_CURSOR_FILE\n";
}
} // namespace

int main(int argc, char **argv)
{
    if (has_flag(argc, argv, "--help"))
    {
        print_usage(argv[0]);
        return 0;
    }

    auto rpc_http = env_or_arg(argc, argv, "--rpc-http", "POLYMARKET_POLYGON_RPC_HTTP");
    auto rpc_ws = env_or_arg(argc, argv, "--rpc-ws", "POLYMARKET_POLYGON_RPC_WS");
    auto ctf = env_or_arg(argc, argv, "--ctf", "POLYMARKET_CONDITIONAL_TOKENS");
    auto uma = env_or_arg(argc, argv, "--uma-adapter", "POLYMARKET_UMA_CTF_ADAPTER");
    auto cursor_file = env_or_arg(argc, argv, "--cursor-file", "POLYMARKET_INDEXER_CURSOR_FILE");
    if (cursor_file.empty())
        cursor_file = "polymarket-indexer-cursors.json";

    if (rpc_http.empty() || (ctf.empty() && uma.empty()))
    {
        print_usage(argv[0]);
        return 1;
    }
    if (has_flag(argc, argv, "--live") && rpc_ws.empty())
    {
        std::cerr << "--live requires --rpc-ws or POLYMARKET_POLYGON_RPC_WS\n";
        return 1;
    }

    polymarket::EvmEventIndexerConfig config;
    config.rpc_http_url = rpc_http;
    config.rpc_ws_url = rpc_ws;
    if (!ctf.empty())
    {
        config.filter = polymarket::conditional_tokens_log_filter(ctf);
        config.cursor_name = "conditional_tokens";
    }
    else
    {
        config.filter = polymarket::uma_adapter_log_filter(uma);
        config.cursor_name = "uma_adapter";
    }

    try
    {
        auto cursor_name = arg_value(argc, argv, "--cursor-name");
        if (!cursor_name.empty())
            config.cursor_name = cursor_name;
        auto batch_size = arg_value(argc, argv, "--batch-size");
        if (!batch_size.empty())
            config.batch_size = std::stoull(batch_size);
        auto confirmations = arg_value(argc, argv, "--confirmations");
        if (!confirmations.empty())
            config.confirmations = std::stoull(confirmations);

        auto start_block = arg_value(argc, argv, "--start-block");
        if (!start_block.empty())
        {
            config.start_block = polymarket::evm_quantity_to_uint64(start_block);
        }
        else
        {
            polymarket::EvmJsonRpcHttpClient http(rpc_http);
            config.start_block = polymarket::evm_quantity_to_uint64(http.block_number());
        }

        auto store = std::make_shared<polymarket::FileBlockCursorStore>(cursor_file);
        polymarket::EvmEventIndexer indexer(config, store);
        indexer.on_error([](const std::string &error)
                         { std::cerr << "[indexer] " << error << "\n"; });
        indexer.on_checkpoint([](uint64_t block)
                              { std::cerr << "[checkpoint] " << block << "\n"; });
        indexer.on_log([](const polymarket::EvmIndexedLog &log)
                       { std::cout << event_json(log).dump() << "\n"; });

        auto report = indexer.catch_up();
        std::cerr << "[catchup] from=" << report.from_block
                  << " to=" << report.to_block
                  << " ranges=" << report.ranges_scanned
                  << " logs=" << report.logs_seen << "\n";

        if (has_flag(argc, argv, "--live"))
        {
            if (!indexer.start_live())
                return 1;
            auto duration = arg_value(argc, argv, "--duration-seconds");
            indexer.run_live_for(std::chrono::seconds(duration.empty() ? 60 : std::stoi(duration)));
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "[indexer] " << error.what() << "\n";
        return 1;
    }
    return 0;
}
