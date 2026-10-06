#include "example_environment.hpp"
#include "polymarket/position_client.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// Check and set the one-time trading approvals of a wallet.
//
//   approvals_example [--execute]
//
// Without --execute the example only reads which approvals are missing;
// nothing is sent. With --execute it grants the missing ones and waits for
// them to be mined.
//
// Environment: PRIVATE_KEY and POLYGON_RPC_ENDPOINT are required. With
// POLYMARKET_PROXY_ADDRESS set, the approvals are set on that Gnosis Safe
// through the relayer in one transaction (gasless; needs RELAYER_API_KEY,
// optional RELAYER_API_KEY_ADDRESS). Otherwise the EOA sends one transaction
// per missing approval and pays gas in POL. POLYMARKET_ENV selects the
// relayer and contract preset (default production).

namespace
{
    std::string env(const char *name)
    {
        const char *value = std::getenv(name);
        return value ? value : "";
    }
} // namespace

int main(int argc, char **argv)
{
    using namespace polymarket;

    bool execute = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--execute") != 0)
        {
            std::cerr << "usage: approvals_example [--execute]\n";
            return 2;
        }
        execute = true;
    }

    auto config = PositionClientConfig::for_environment(polymarket_example::selected_environment());
    config.private_key = env("PRIVATE_KEY");
    config.rpc_url = env("POLYGON_RPC_ENDPOINT");
    if (config.private_key.empty() || config.rpc_url.empty())
    {
        std::cout << "PRIVATE_KEY or POLYGON_RPC_ENDPOINT not set; skipping approvals example.\n";
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
        const auto state = client.get_trading_approvals_state();
        std::cout << "wallet " << client.wallet_address()
                  << (config.wallet_type == SignatureType::EOA ? " (EOA)" : " (Safe via relayer)")
                  << '\n';
        for (const auto &approval : state.missing.erc20)
            std::cout << "missing ERC-20  " << approval.token_address << " -> " << approval.spender
                      << '\n';
        for (const auto &approval : state.missing.erc1155)
            std::cout << "missing ERC-1155 " << approval.token_address << " -> "
                      << approval.operator_address << '\n';
        if (state.is_fully_approved)
        {
            std::cout << "all trading approvals are set\n";
            return 0;
        }
        if (!execute)
        {
            std::cout << "dry run: pass --execute to grant "
                      << state.missing.erc20.size() + state.missing.erc1155.size()
                      << " approval(s)\n";
            return 0;
        }

        std::cout << "granting approvals and waiting for confirmation...\n";
        const auto outcome = client.setup_trading_approvals();
        if (outcome)
            std::cout << "confirmed " << outcome->transaction_hash << " in block "
                      << outcome->receipt.block_number << '\n';
    }
    catch (const PartialBatchError &error)
    {
        // EOA only: approvals already mined stay in place; rerun to finish.
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
