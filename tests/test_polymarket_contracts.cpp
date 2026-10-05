#include "polymarket/clob_client.hpp"
#include "polymarket/order_signer.hpp"
#include "polymarket/polymarket_contracts.hpp"
#include <cctype>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using polymarket::PolymarketContracts;

    int failures = 0;

    void check(bool condition, const std::string &message)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << message << "\n";
    }

    void expect_invalid(const std::string &name, const std::function<void()> &action)
    {
        try
        {
            action();
        }
        catch (const std::invalid_argument &)
        {
            return;
        }
        catch (...)
        {
        }
        check(false, name + " did not throw std::invalid_argument");
    }

    std::string lowercase(std::string value)
    {
        for (auto &c : value)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }

    // Mixed-case addresses must carry a valid EIP-55 checksum, which catches
    // transcription errors. All-lowercase addresses carry no checksum.
    bool has_valid_checksum_or_none(const std::string &address)
    {
        const auto hex = address.substr(2);
        if (hex == lowercase(hex))
            return true;
        const auto hash = polymarket::keccak256(lowercase(hex));
        for (size_t i = 0; i < hex.size(); ++i)
        {
            if (!std::isalpha(static_cast<unsigned char>(hex[i])))
                continue;
            const int nibble = (hash[i / 2] >> (i % 2 == 0 ? 4 : 0)) & 0x0f;
            if ((nibble >= 8) != static_cast<bool>(std::isupper(static_cast<unsigned char>(hex[i]))))
                return false;
        }
        return true;
    }

    std::vector<std::pair<std::string, std::string>> address_fields(const PolymarketContracts &c)
    {
        return {{"collateral_token", c.collateral_token},
                {"conditional_tokens", c.conditional_tokens},
                {"neg_risk_adapter", c.neg_risk_adapter},
                {"collateral_adapter", c.collateral_adapter},
                {"neg_risk_collateral_adapter", c.neg_risk_collateral_adapter},
                {"standard_exchange", c.standard_exchange},
                {"neg_risk_exchange", c.neg_risk_exchange},
                {"exchange_v3", c.exchange_v3},
                {"protocol_v2_router", c.protocol_v2_router},
                {"position_manager", c.position_manager},
                {"binary_module", c.binary_module},
                {"neg_risk_module", c.neg_risk_module},
                {"combinatorial_module", c.combinatorial_module},
                {"auto_redeem_operator", c.auto_redeem_operator},
                {"proxy_factory", c.proxy_factory},
                {"proxy_implementation", c.proxy_implementation},
                {"safe_factory", c.safe_factory},
                {"safe_multisend", c.safe_multisend},
                {"relay_hub", c.relay_hub},
                {"deposit_wallet_factory", c.deposit_wallet_factory},
                {"deposit_wallet_implementation", c.deposit_wallet_implementation},
                {"deposit_wallet_beacon", c.deposit_wallet_beacon}};
    }

    void mainnet_preset()
    {
        const auto c = PolymarketContracts::polygon_mainnet();
        c.validate();
        check(c.chain_id == 137, "mainnet chain id");
        for (const auto &[field, address] : address_fields(c))
            check(has_valid_checksum_or_none(address), field + " has a bad EIP-55 checksum: " + address);

        // Spot-check the addresses split/merge/redeem depend on against the
        // official SDK environments (py-sdk environments.py, ts-sdk environments.ts).
        check(c.collateral_token == "0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB", "pUSD address");
        check(c.conditional_tokens == "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045", "CTF address");
        check(c.ctf_collateral_adapter(false) == "0xAdA100Db00Ca00073811820692005400218FcE1f",
              "standard collateral adapter");
        check(c.ctf_collateral_adapter(true) == "0xadA2005600Dec949baf300f4C6120000bDB6eAab",
              "neg-risk collateral adapter");
        check(c.protocol_v2_router == "0x12121212006e4CD160D18e3f00711DA5c3372600", "V2 router address");
        check(c.safe_multisend == "0xA238CBeb142c10Ef7Ad8442C6D1f9E89e07e7761", "Safe MultiSend address");
        check(c.exchange(false) == c.standard_exchange && c.exchange(true) == c.neg_risk_exchange,
              "exchange selector");

        check(PolymarketContracts::for_chain(137).collateral_adapter == c.collateral_adapter, "for_chain(137)");
        expect_invalid("for_chain(80002)", []
                       { (void)PolymarketContracts::for_chain(80002); });
    }

    void validation()
    {
        const auto rejects = [](const std::string &name, const std::function<void(PolymarketContracts &)> &mutate)
        {
            auto c = PolymarketContracts::polygon_mainnet();
            mutate(c);
            expect_invalid(name, [&]
                           { c.validate(); });
        };
        rejects("zero chain id", [](auto &c)
                { c.chain_id = 0; });
        rejects("empty adapter", [](auto &c)
                { c.collateral_adapter.clear(); });
        rejects("unprefixed address", [](auto &c)
                { c.collateral_token = c.collateral_token.substr(2); });
        rejects("short address", [](auto &c)
                { c.safe_multisend.pop_back(); c.safe_multisend.pop_back(); });
        rejects("non-hex address", [](auto &c)
                { c.protocol_v2_router.back() = 'g'; });
        rejects("init code hash as address", [](auto &c)
                { c.safe_init_code_hash = c.safe_factory; });
    }

    void clob_client_uses_preset()
    {
        polymarket::ClobClient client("http://127.0.0.1:1");
        const auto c = PolymarketContracts::polygon_mainnet();
        check(client.get_exchange_address() == c.standard_exchange, "ClobClient standard exchange");
        check(client.get_neg_risk_exchange_address() == c.neg_risk_exchange, "ClobClient neg-risk exchange");
    }
} // namespace

int main()
{
    mainnet_preset();
    validation();
    clob_client_uses_preset();
    if (failures != 0)
    {
        std::cerr << failures << " test_polymarket_contracts check(s) failed\n";
        return 1;
    }
    std::cout << "test_polymarket_contracts passed\n";
    return 0;
}
