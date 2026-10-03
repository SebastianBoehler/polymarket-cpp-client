#include "polymarket_contracts.hpp"
#include "order_signer.hpp"
#include <stdexcept>
#include <utility>
#include <vector>

namespace polymarket
{
    namespace
    {
        void require_hex_bytes(const std::string &field, const std::string &value, size_t size)
        {
            bool ok = value.rfind("0x", 0) == 0;
            if (ok)
            {
                try
                {
                    ok = from_hex(value).size() == size;
                }
                catch (const std::invalid_argument &)
                {
                    ok = false;
                }
            }
            if (!ok)
                throw std::invalid_argument("PolymarketContracts." + field + " must be 0x-prefixed " +
                                            std::to_string(size) + "-byte hex");
        }
    } // namespace

    PolymarketContracts PolymarketContracts::polygon_mainnet()
    {
        PolymarketContracts c;
        c.chain_id = 137;
        c.collateral_token = "0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB";
        c.conditional_tokens = "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045";
        c.neg_risk_adapter = "0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296";
        c.collateral_adapter = "0xAdA100Db00Ca00073811820692005400218FcE1f";
        c.neg_risk_collateral_adapter = "0xadA2005600Dec949baf300f4C6120000bDB6eAab";
        c.standard_exchange = "0xE111180000d2663C0091e4f400237545B87B996B";
        c.neg_risk_exchange = "0xe2222d279d744050d28e00520010520000310F59";
        c.exchange_v3 = "0xe3333700cA9d93003F00f0F71f8515005F6c00Aa";
        c.protocol_v2_router = "0x12121212006e4CD160D18e3f00711DA5c3372600";
        c.position_manager = "0x006F54F7f9A22e0000CC2AB60031000000ae9fEF";
        c.binary_module = "0x1000008dD9001B968442c1000017eaE6E0dA00Ba";
        c.neg_risk_module = "0x200000900045e3B6259600682756002200028933";
        c.combinatorial_module = "0x30000034706c7d8e12009dab006be20000c031a8";
        c.auto_redeem_operator = "0xa1200000d0002264C9a1698e001292D00E1b00af";
        c.proxy_factory = "0xaB45c5A4B0c941a2F231C04C3f49182e1A254052";
        c.proxy_implementation = "0x44e999d5c2F66Ef0861317f9A4805AC2e90aEB4f";
        c.safe_factory = "0xaacFeEa03eb1561C4e67d661e40682Bd20E3541b";
        c.safe_init_code_hash = "0x2bce2127ff07fb632d16c8347c4ebf501f4841168bed00d9e6ef715ddb6fcecf";
        c.safe_multisend = "0xA238CBeb142c10Ef7Ad8442C6D1f9E89e07e7761";
        c.relay_hub = "0xD216153c06E857cD7f72665E0aF1d7D82172F494";
        c.deposit_wallet_factory = "0x00000000000Fb5C9ADea0298D729A0CB3823Cc07";
        c.deposit_wallet_implementation = "0x58CA52ebe0DadfdF531Cde7062e76746de4Db1eB";
        c.deposit_wallet_beacon = "0x7A18EDfe055488A3128f01F563e5B479D92ffc3a";
        return c;
    }

    PolymarketContracts PolymarketContracts::for_chain(uint64_t chain_id)
    {
        if (chain_id == 137)
            return polygon_mainnet();
        throw std::invalid_argument("no Polymarket contract preset for chain " + std::to_string(chain_id));
    }

    void PolymarketContracts::validate() const
    {
        if (chain_id == 0)
            throw std::invalid_argument("PolymarketContracts.chain_id must be non-zero");
        const std::vector<std::pair<const char *, const std::string *>> addresses = {
            {"collateral_token", &collateral_token},
            {"conditional_tokens", &conditional_tokens},
            {"neg_risk_adapter", &neg_risk_adapter},
            {"collateral_adapter", &collateral_adapter},
            {"neg_risk_collateral_adapter", &neg_risk_collateral_adapter},
            {"standard_exchange", &standard_exchange},
            {"neg_risk_exchange", &neg_risk_exchange},
            {"exchange_v3", &exchange_v3},
            {"protocol_v2_router", &protocol_v2_router},
            {"position_manager", &position_manager},
            {"binary_module", &binary_module},
            {"neg_risk_module", &neg_risk_module},
            {"combinatorial_module", &combinatorial_module},
            {"auto_redeem_operator", &auto_redeem_operator},
            {"proxy_factory", &proxy_factory},
            {"proxy_implementation", &proxy_implementation},
            {"safe_factory", &safe_factory},
            {"safe_multisend", &safe_multisend},
            {"relay_hub", &relay_hub},
            {"deposit_wallet_factory", &deposit_wallet_factory},
            {"deposit_wallet_implementation", &deposit_wallet_implementation},
            {"deposit_wallet_beacon", &deposit_wallet_beacon},
        };
        for (const auto &[field, value] : addresses)
            require_hex_bytes(field, *value, 20);
        require_hex_bytes("safe_init_code_hash", safe_init_code_hash, 32);
    }

} // namespace polymarket
