#include "json_rpc_client.hpp"
#include "polymarket_events.hpp"
#include "websocket_client.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

using json = nlohmann::json;

namespace
{
uint64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

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

std::vector<std::string> split_csv(const std::string &csv)
{
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        if (!item.empty())
            out.push_back(item);
    }
    return out;
}

uint64_t parse_u64(const std::string &value)
{
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
        return std::stoull(value.substr(2), nullptr, 16);
    return std::stoull(value);
}

struct FeedStats
{
    std::string name;
    uint64_t started_ms{0};
    uint64_t first_message_ms{0};
    uint64_t messages{0};
    uint64_t timestamped_messages{0};
    uint64_t bytes{0};
    std::vector<int64_t> lags_ms;
    std::map<std::string, std::vector<int64_t>> lags_by_type;
    std::map<std::string, uint64_t> types;
    mutable std::mutex mutex;

    explicit FeedStats(std::string feed_name) : name(std::move(feed_name)) {}

    void start()
    {
        std::lock_guard<std::mutex> lock(mutex);
        started_ms = now_ms();
    }

    void record(const std::string &type, size_t message_bytes,
                std::optional<uint64_t> event_time_ms)
    {
        auto received_ms = now_ms();
        std::lock_guard<std::mutex> lock(mutex);
        if (first_message_ms == 0)
            first_message_ms = received_ms;
        messages++;
        bytes += message_bytes;
        types[type]++;
        if (event_time_ms)
        {
            timestamped_messages++;
            int64_t lag = static_cast<int64_t>(received_ms) -
                          static_cast<int64_t>(*event_time_ms);
            lags_ms.push_back(lag);
            lags_by_type[type].push_back(lag);
        }
    }

    json summary() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        json out = {
            {"feed", name},
            {"messages", messages},
            {"timestamped_messages", timestamped_messages},
            {"bytes", bytes},
            {"types", types},
            {"first_message_after_ms", first_message_ms && started_ms ? first_message_ms - started_ms : 0}};
        auto lag_summary = [](std::vector<int64_t> values)
        {
            std::sort(values.begin(), values.end());
            auto pct = [&values](double p)
            {
                size_t idx = static_cast<size_t>((values.size() - 1) * p);
                return values[idx];
            };
            int64_t sum = 0;
            for (auto lag : values)
                sum += lag;
            return json{
                {"min", values.front()},
                {"p50", pct(0.50)},
                {"p95", pct(0.95)},
                {"max", values.back()},
                {"avg", static_cast<double>(sum) / values.size()}};
        };
        if (!lags_ms.empty())
        {
            out["lag_ms"] = lag_summary(lags_ms);
            json by_type = json::object();
            for (const auto &[type, values] : lags_by_type)
                by_type[type] = lag_summary(values);
            out["lag_ms_by_type"] = by_type;
        }
        return out;
    }
};

std::optional<uint64_t> json_timestamp_ms(const json &message)
{
    if (!message.contains("timestamp"))
        return std::nullopt;
    std::string raw = message["timestamp"].is_string()
                          ? message["timestamp"].get<std::string>()
                          : std::to_string(message["timestamp"].get<uint64_t>());
    auto value = parse_u64(raw);
    return value < 100000000000ULL ? std::optional<uint64_t>(value * 1000)
                                   : std::optional<uint64_t>(value);
}

void record_polymarket_payload(FeedStats &stats, const json &payload)
{
    if (payload.is_array())
    {
        for (const auto &item : payload)
            record_polymarket_payload(stats, item);
        return;
    }
    if (!payload.is_object())
        return;
    std::string type = payload.value("event_type", "unknown");
    stats.record(type, payload.dump().size(), json_timestamp_ms(payload));
}

void print_usage(const char *program)
{
    std::cerr << "Usage: " << program
              << " --asset-id <id[,id]> --rpc-ws <wss-url> [--ctf <addr>] "
              << "[--uma-adapter <addr>] [--pending] [--custom-feature] "
              << "[--duration-seconds <n>]\n"
              << "Env: POLYMARKET_POLYGON_RPC_WS, POLYMARKET_CONDITIONAL_TOKENS, "
              << "POLYMARKET_UMA_CTF_ADAPTER\n";
}
} // namespace

int main(int argc, char **argv)
{
    if (has_flag(argc, argv, "--help"))
    {
        print_usage(argv[0]);
        return 0;
    }

    auto asset_ids = split_csv(arg_value(argc, argv, "--asset-id"));
    auto rpc_ws = env_or_arg(argc, argv, "--rpc-ws", "POLYMARKET_POLYGON_RPC_WS");
    auto ctf = env_or_arg(argc, argv, "--ctf", "POLYMARKET_CONDITIONAL_TOKENS");
    auto uma = env_or_arg(argc, argv, "--uma-adapter", "POLYMARKET_UMA_CTF_ADAPTER");
    auto duration_arg = arg_value(argc, argv, "--duration-seconds");
    int duration_seconds = duration_arg.empty() ? 30 : std::stoi(duration_arg);
    if (asset_ids.empty() || rpc_ws.empty() || duration_seconds <= 0)
    {
        print_usage(argv[0]);
        return 1;
    }

    FeedStats polymarket_stats("polymarket_market_ws");
    FeedStats polygon_stats("polygon_rpc_ws");
    std::atomic<bool> poly_connected{false};

    polymarket::WebSocketClient poly_ws;
    poly_ws.set_url("wss://ws-subscriptions-clob.polymarket.com/ws/market");
    poly_ws.set_auto_reconnect(false);
    poly_ws.on_connect([&]()
                       {
        poly_connected.store(true);
        json subscribe = {
            {"type", "market"},
            {"assets_ids", asset_ids}};
        if (has_flag(argc, argv, "--custom-feature"))
            subscribe["custom_feature_enabled"] = true;
        poly_ws.send(subscribe.dump()); });
    poly_ws.on_error([](const std::string &error)
                     { std::cerr << "[polymarket] " << error << "\n"; });
    poly_ws.on_message([&](const std::string &raw)
                       {
        try
        {
            auto message = json::parse(raw);
            record_polymarket_payload(polymarket_stats, message);
        }
        catch (const std::exception &error)
        {
            std::cerr << "[polymarket parse] " << error.what() << "\n";
        } });

    polymarket::EvmJsonRpcWsClient polygon_ws(rpc_ws);
    polygon_ws.on_error([](const std::string &error)
                        { std::cerr << "[polygon] " << error << "\n"; });
    polygon_ws.on_head([&](const json &head)
                       {
        std::optional<uint64_t> timestamp;
        if (head.contains("timestamp"))
            timestamp = parse_u64(head["timestamp"].get<std::string>()) * 1000;
        polygon_stats.record("newHeads", head.dump().size(), timestamp); });
    polygon_ws.on_log([&](const polymarket::EvmLog &log)
                      { polygon_stats.record("logs", log.data.size(), std::nullopt); });
    polygon_ws.on_pending_transaction([&](const std::string &tx)
                                      { polygon_stats.record("pending_tx", tx.size(), std::nullopt); });

    if (!ctf.empty())
        polygon_ws.subscribe_logs(polymarket::conditional_tokens_log_filter(ctf));
    if (!uma.empty())
        polygon_ws.subscribe_logs(polymarket::uma_adapter_log_filter(uma));
    polygon_ws.subscribe_new_heads();
    if (has_flag(argc, argv, "--pending"))
        polygon_ws.subscribe_pending_transactions();

    polymarket_stats.start();
    polygon_stats.start();
    poly_ws.connect();
    polygon_ws.connect();

    for (int i = 0; i < duration_seconds; ++i)
        std::this_thread::sleep_for(std::chrono::seconds(1));

    poly_ws.stop();
    polygon_ws.stop();

    json result = {
        {"duration_seconds", duration_seconds},
        {"asset_ids", asset_ids},
        {"notes", "Latency is receive_time - source_timestamp where source timestamp exists. Feeds do not carry identical event domains."},
        {"feeds", json::array({polymarket_stats.summary(), polygon_stats.summary()})}};
    std::cout << result.dump(2) << "\n";
    return 0;
}
