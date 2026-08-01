#pragma once

#include "evm_utils.hpp"
#include "http_client.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace polymarket
{
    namespace detail
    {
        class EvmJsonRpcWsRuntime;
    }

    class EvmJsonRpcHttpClient
    {
    public:
        explicit EvmJsonRpcHttpClient(const std::string &rpc_url);

        void set_timeout_ms(long timeout_ms);
        void set_proxy(const std::string &proxy_url);

        nlohmann::json call(const std::string &method, const nlohmann::json &params);
        std::string block_number();
        std::vector<EvmLog> get_logs(const EvmLogFilter &filter);
        nlohmann::json get_transaction_by_hash(const std::string &tx_hash);

    private:
        HttpClient http_;
        std::atomic<uint64_t> next_id_{1};
    };

    using EvmLogCallback = std::function<void(const EvmLog &)>;
    using EvmPendingTxCallback = std::function<void(const std::string &)>;
    using EvmJsonCallback = std::function<void(const nlohmann::json &)>;
    using EvmRpcErrorCallback = std::function<void(const std::string &)>;

    class EvmJsonRpcWsClient
    {
    public:
        explicit EvmJsonRpcWsClient(const std::string &rpc_ws_url);
        ~EvmJsonRpcWsClient();

        EvmJsonRpcWsClient(const EvmJsonRpcWsClient &) = delete;
        EvmJsonRpcWsClient &operator=(const EvmJsonRpcWsClient &) = delete;

        void set_ping_interval_ms(int interval_ms);
        void set_auto_reconnect(bool enabled);

        void on_log(EvmLogCallback callback);
        void on_pending_transaction(EvmPendingTxCallback callback);
        void on_head(EvmJsonCallback callback);
        void on_error(EvmRpcErrorCallback callback);

        bool connect();
        void disconnect();
        bool is_connected() const;
        void run();
        void stop();

        bool subscribe_logs(const EvmLogFilter &filter);
        bool subscribe_pending_transactions();
        bool subscribe_new_heads();

    private:
        std::shared_ptr<detail::EvmJsonRpcWsRuntime> runtime_;
    };

} // namespace polymarket
