#include "evm_indexer_transport.hpp"
#include "evm_transport_epoch.hpp"
#include "json_rpc_client.hpp"
#include "websocket_client.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>

namespace polymarket
{
    namespace
    {
        class JsonRpcIndexerTransport final : public EvmEventIndexerTransport
        {
        public:
            explicit JsonRpcIndexerTransport(const EvmEventIndexerConfig &config)
                : http_(config.rpc_http_url)
            {
                ws_.set_url(config.rpc_ws_url);
                ws_.set_ping_interval_ms(config.ws_ping_interval_ms);
                ws_.on_stream_gap([this](uint64_t generation)
                                  { epoch_.advance(generation, [this, generation]
                                                   {
                                                       if (generation > 1)
                                                           fail("EVM websocket stream gap detected at generation " +
                                                                std::to_string(generation));
                                                   }); });
            }

            std::string block_number() override { return http_.block_number(); }

            std::vector<EvmLog> get_logs(const EvmLogFilter &filter) override
            {
                return http_.get_logs(filter);
            }

            bool start_logs(const EvmLogFilter &filter, EvmLogCallback log_callback,
                            EvmHeadCallback head_callback,
                            EvmRpcErrorCallback error_callback) override
            {
                log_callback_ = std::move(log_callback);
                head_callback_ = std::move(head_callback);
                error_callback_ = std::move(error_callback);
                log_request_ = {
                    {"jsonrpc", "2.0"},
                    {"id", log_request_id_},
                    {"method", "eth_subscribe"},
                    {"params", nlohmann::json::array({"logs", filter.to_json()})}};
                if (head_callback_)
                {
                    head_request_ = {
                        {"jsonrpc", "2.0"},
                        {"id", head_request_id_},
                        {"method", "eth_subscribe"},
                        {"params", nlohmann::json::array({"newHeads"})}};
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    log_ready_ = false;
                    head_ready_ = !head_callback_;
                    subscription_failed_ = false;
                }

                ws_.on_sequenced_message(
                    [this](const std::string &message, uint64_t generation)
                    { epoch_.admit(generation, [this, &message]
                                   { handle_message(message); }); });
                ws_.on_connect([this]() { send_subscriptions(); });
                ws_.on_error([this](const std::string &error) { fail(error); });
                if (!ws_.connect())
                    return false;

                std::unique_lock<std::mutex> lock(mutex_);
                const bool signalled = ready_cv_.wait_for(
                    lock, std::chrono::seconds(10),
                    [this]() { return (log_ready_ && head_ready_) || subscription_failed_; });
                if (!signalled)
                {
                    lock.unlock();
                    fail("timed out waiting for EVM log subscription acknowledgement");
                    return false;
                }
                return log_ready_ && head_ready_;
            }

            void stop() override { ws_.stop(); }

        private:
            EvmJsonRpcHttpClient http_;
            WebSocketClient ws_;
            detail::EvmTransportEpoch epoch_;
            EvmLogCallback log_callback_;
            EvmHeadCallback head_callback_;
            EvmRpcErrorCallback error_callback_;
            nlohmann::json log_request_;
            nlohmann::json head_request_;
            std::mutex mutex_;
            std::condition_variable ready_cv_;
            bool log_ready_{false};
            bool head_ready_{false};
            bool subscription_failed_{false};
            std::string log_subscription_id_;
            std::string head_subscription_id_;
            static constexpr uint64_t log_request_id_{1};
            static constexpr uint64_t head_request_id_{2};

            void send_subscriptions()
            {
                if (!ws_.send(log_request_.dump()))
                    fail("failed to send EVM log subscription");
                if (head_callback_ && !ws_.send(head_request_.dump()))
                    fail("failed to send EVM head subscription");
            }

            void handle_message(const std::string &message)
            {
                try
                {
                    const auto body = nlohmann::json::parse(message);
                    const auto response_id = body.value("id", uint64_t{0});
                    if (response_id == log_request_id_ || response_id == head_request_id_)
                    {
                        if (body.contains("error"))
                        {
                            fail("EVM log subscription rejected: " + body["error"].dump());
                            return;
                        }
                        if (body.contains("result") && body["result"].is_string())
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (response_id == log_request_id_)
                            {
                                log_subscription_id_ = body["result"].get<std::string>();
                                log_ready_ = true;
                            }
                            else
                            {
                                head_subscription_id_ = body["result"].get<std::string>();
                                head_ready_ = true;
                            }
                            ready_cv_.notify_all();
                        }
                        return;
                    }

                    if (body.value("method", "") != "eth_subscription" ||
                        !body.contains("params") || !body["params"].contains("result"))
                        return;
                    const auto subscription_id = body["params"].value("subscription", "");
                    const auto &result = body["params"]["result"];
                    std::string log_subscription_id;
                    std::string head_subscription_id;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        log_subscription_id = log_subscription_id_;
                        head_subscription_id = head_subscription_id_;
                    }
                    if (subscription_id == log_subscription_id && result.is_object() && log_callback_)
                        log_callback_(evm_log_from_json(result));
                    else if (subscription_id == head_subscription_id && result.is_object() &&
                             result.contains("number") && head_callback_)
                        head_callback_(evm_quantity_to_uint64(result["number"].get<std::string>()));
                }
                catch (const std::exception &error)
                {
                    fail(std::string("invalid EVM subscription message: ") + error.what());
                }
            }

            void fail(const std::string &error)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!(log_ready_ && head_ready_))
                        subscription_failed_ = true;
                }
                ready_cv_.notify_all();
                if (error_callback_)
                    error_callback_(error);
            }
        };
    } // namespace

    std::shared_ptr<EvmEventIndexerTransport>
    make_json_rpc_indexer_transport(const EvmEventIndexerConfig &config)
    {
        return std::make_shared<JsonRpcIndexerTransport>(config);
    }

} // namespace polymarket
