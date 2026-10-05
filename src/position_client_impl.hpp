#pragma once

#include "polymarket/http_client.hpp"
#include "polymarket/order_signer.hpp"
#include "polymarket/position_client.hpp"
#include "safe_relayer.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace polymarket
{
    // Shared state of a PositionClient and its two ways of sending calls.
    struct PositionClient::Impl
    {
        PolymarketContracts contracts;
        OrderSigner signer;
        std::string signer_address;
        std::string wallet; // EOA or Safe that holds positions
        SignatureType wallet_type;
        std::shared_ptr<EvmJsonRpcHttpClient> rpc;
        std::shared_ptr<detail::RelayerClient> relayer; // Safe only
        HttpClient gamma;
        std::chrono::milliseconds relayer_retry_delay;
        int relayer_max_submit_retries;
        std::mutex send_mutex; // one nonce sequence at a time
        bool chain_verified{false};

        explicit Impl(PositionClientConfig &config);

        // Fails once if the RPC node is on another chain. Caller holds send_mutex.
        void ensure_chain();
        // Signs and broadcasts one call from the EOA. Caller holds send_mutex.
        std::string send_from_eoa(const ContractCall &call);

        TransactionHandle execute_from_eoa(const std::vector<ContractCall> &calls);
        TransactionHandle execute_from_safe(const std::vector<ContractCall> &calls, const std::string &metadata);
    };
} // namespace polymarket
