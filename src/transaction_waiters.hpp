#pragma once

#include "polymarket/json_rpc_client.hpp"
#include "polymarket/position_client.hpp"
#include "safe_relayer.hpp"
#include <chrono>
#include <string>

namespace polymarket::detail
{
    // Polls eth_getTransactionReceipt until the transaction is mined.
    TransactionOutcome wait_for_receipt(EvmJsonRpcHttpClient &rpc, const std::string &transaction_hash,
                                        std::chrono::milliseconds timeout, std::chrono::milliseconds poll_interval);

    // Polls the relayer until the transaction settles, then reads its receipt.
    TransactionOutcome wait_for_relayer(RelayerClient &relayer, EvmJsonRpcHttpClient &rpc,
                                        const std::string &transaction_id, const std::string &submit_hash,
                                        std::chrono::milliseconds timeout, std::chrono::milliseconds poll_interval);
}
