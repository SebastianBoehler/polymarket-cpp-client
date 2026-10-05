#include "polymarket/position_client.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// Split, merge or redeem a binary market position.
//
//   position_example <split|merge|redeem> <condition_id> [amount] [--execute]
//
// condition_id is 0x + 32 bytes of hex; a Protocol V2 market also accepts its
// 31-byte id. amount is in base units (6 decimals): 1000000 = 1 pUSD / 1 share. merge
// defaults to "max". Without --execute the example only resolves the market
// and prints the wallet's balances; nothing is sent.
//
// Environment: PRIVATE_KEY and POLYGON_RPC_ENDPOINT are required. With
// POLYMARKET_PROXY_ADDRESS set, operations run from that Gnosis Safe through
// the relayer (gasless; needs RELAYER_API_KEY, optional RELAYER_API_KEY_ADDRESS).
// Otherwise they are sent from the EOA, which pays gas in POL.

namespace
{
    std::string env(const char *name)
    {
        const char *value = std::getenv(name);
        return value ? value : "";
    }

    int usage()
    {
        std::cerr << "usage: position_example <split|merge|redeem> <condition_id> [amount] [--execute]\n";
        return 2;
    }
}

int main(int argc, char **argv)
{
    using namespace polymarket;

    if (argc < 3)
        return usage();
    const std::string action = argv[1];
    const std::string condition_id = argv[2];
    std::string amount = action == "merge" ? "max" : "";
    bool execute = false;
    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--execute") == 0)
            execute = true;
        else
            amount = argv[i];
    }
    if ((action != "split" && action != "merge" && action != "redeem") || (action == "split" && amount.empty()))
        return usage();

    PositionClientConfig config;
    config.private_key = env("PRIVATE_KEY");
    config.rpc_url = env("POLYGON_RPC_ENDPOINT");
    if (config.private_key.empty() || config.rpc_url.empty())
    {
        std::cout << "PRIVATE_KEY or POLYGON_RPC_ENDPOINT not set; skipping position example.\n";
        return 0;
    }
    config.funder_address = env("POLYMARKET_PROXY_ADDRESS");
    if (!config.funder_address.empty())
    {
        config.wallet_type = SignatureType::POLY_GNOSIS_SAFE;
        config.relayer_api_key = env("RELAYER_API_KEY");
        config.relayer_api_key_address = env("RELAYER_API_KEY_ADDRESS");
    }

    try
    {
        PositionClient client(config);
        const auto market = client.resolve_market(condition_id, action == "redeem");
        const auto balances = client.position_balances(market);
        std::cout << "wallet   " << client.wallet_address()
                  << (config.wallet_type == SignatureType::EOA ? " (EOA)" : " (Safe via relayer)") << '\n'
                  << "market   " << market.market_id << ' '
                  << (market.protocol == MarketProtocol::Ctf ? "CTF" : "Protocol V2")
                  << (market.neg_risk ? " neg-risk" : "") << '\n'
                  << "operator " << market.operator_contract << '\n'
                  << "balances yes=" << balances[0] << " no=" << balances[1] << " (base units)\n";

        if (!execute)
        {
            std::cout << "dry run: pass --execute to " << action
                      << (amount.empty() ? "" : " " + amount) << '\n';
            return 0;
        }

        const auto handle = action == "split"   ? client.split_position(condition_id, amount)
                            : action == "merge" ? client.merge_positions(condition_id, amount)
                                                : client.redeem_positions(condition_id);
        if (!handle.transaction_id().empty())
            std::cout << "relayer transaction " << handle.transaction_id() << '\n';
        if (!handle.transaction_hash().empty())
            std::cout << "transaction " << handle.transaction_hash() << '\n';
        std::cout << "waiting for confirmation...\n";

        const auto outcome = handle.wait();
        std::cout << "confirmed " << outcome.transaction_hash << " in block " << outcome.receipt.block_number
                  << ", gas used " << outcome.receipt.gas_used << '\n';
    }
    catch (const TransactionRevertedError &error)
    {
        std::cerr << "reverted: " << error.transaction_hash() << '\n';
        return 1;
    }
    catch (const PartialBatchError &error)
    {
        // EOA only: a V2 redeem of both outcomes sends one transaction per outcome.
        std::cerr << "error: " << error.what() << '\n';
        for (const auto &hash : error.submitted_hashes())
            std::cerr << "already sent: " << hash << '\n';
        return 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
