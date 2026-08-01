#include "order_test_live.hpp"

#include "clob_client.hpp"
#include "http_client.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

namespace polymarket::order_test
{
    namespace
    {
        using json = nlohmann::json;

        struct LiveMarketData
        {
            std::string token_id;
            std::string slug;
            double best_ask = 0.0;
            std::string tick_size;
            bool neg_risk = false;
        };

        LiveMarketData find_live_market()
        {
            LiveMarketData live_market;
            const auto now_ts = static_cast<uint64_t>(std::time(nullptr));
            constexpr uint64_t min_time_left = 2 * 60;
            ClobClient market_data_client(CLOB_API, 137);
            market_data_client.set_user_agent("polymarket-cpp-client/order-test");

            std::vector<std::pair<uint64_t, uint64_t>> candidates;
            const uint64_t current_window = (now_ts / 900) * 900;
            for (int i = 0; i <= 3; i++)
            {
                const uint64_t start_ts = current_window + i * 900;
                const uint64_t expiry_ts = start_ts + 900;
                if (expiry_ts > now_ts + min_time_left)
                {
                    candidates.push_back({start_ts, expiry_ts});
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                      [](const auto &a, const auto &b)
                      { return a.second < b.second; });

            for (const auto &[target_ts, expiry_ts] : candidates)
            {
                const std::string slug = "btc-updown-15m-" + std::to_string(target_ts);
                const uint64_t time_left = expiry_ts - now_ts;

                HttpClient gamma_http;
                gamma_http.set_base_url("https://gamma-api.polymarket.com");
                gamma_http.set_timeout_ms(10000);
                gamma_http.set_user_agent("polymarket-cpp-client/order-test");

                const auto gamma_response = gamma_http.get("/events?slug=" + slug);
                if (!gamma_response.ok())
                {
                    continue;
                }

                auto gamma_json = json::parse(gamma_response.body);
                if (!gamma_json.is_array() || gamma_json.empty())
                {
                    continue;
                }

                auto &event = gamma_json[0];
                if (!event.contains("markets") || event["markets"].empty())
                {
                    continue;
                }

                auto &market = event["markets"][0];
                auto token_ids = json::parse(market["clobTokenIds"].get<std::string>());
                std::string candidate_token = token_ids[0].get<std::string>();

                auto book = market_data_client.get_order_book(candidate_token);
                if (!book || book->asks.empty())
                {
                    std::cout << "    Skipping " << slug << " - no ask liquidity\n";
                    continue;
                }

                const double candidate_best_ask = book->best_ask();
                if (candidate_best_ask <= 0.0 || candidate_best_ask >= 1.0)
                {
                    std::cout << "    Skipping " << slug << " - invalid best ask " << candidate_best_ask << "\n";
                    continue;
                }

                auto tick_size = market_data_client.get_tick_size(candidate_token);
                if (!tick_size || tick_size->minimum_tick_size.empty())
                {
                    std::cout << "    Skipping " << slug << " - could not fetch tick size\n";
                    continue;
                }

                auto neg_risk = market_data_client.get_neg_risk(candidate_token);
                if (!neg_risk)
                {
                    std::cout << "    Skipping " << slug << " - could not fetch neg_risk\n";
                    continue;
                }

                live_market.token_id = candidate_token;
                live_market.slug = slug;
                live_market.best_ask = candidate_best_ask;
                live_market.tick_size = tick_size->minimum_tick_size;
                live_market.neg_risk = neg_risk->neg_risk;

                std::cout << "    Found market with liquidity: " << slug << " (expires in " << time_left / 60 << "min)\n";
                std::cout << "    Best ask: " << live_market.best_ask << "\n";
                std::cout << "    Tick size: " << live_market.tick_size << "\n";
                std::cout << "    neg_risk: " << (live_market.neg_risk ? "true" : "false") << "\n";
                break;
            }

            return live_market;
        }
    }

    bool run_live_order(const std::string &private_key,
                        const std::string &funder_address,
                        const ApiCredentials &credentials,
                        bool have_credentials,
                        const OrderSigner &signer)
    {
        std::cout << "\n[7] LIVE MODE - Placing $1 test order on BTC market...\n";
        std::cout << "    Fetching nearest active BTC 15m market...\n";

        const LiveMarketData live_market = find_live_market();
        if (live_market.token_id.empty() || live_market.best_ask <= 0.0 || live_market.tick_size.empty())
        {
            std::cerr << "    Could not find active BTC 15m market with complete trading metadata\n";
            return false;
        }

        std::cout << "    YES token: " << live_market.token_id.substr(0, 30) << "...\n";

        const std::string exchange_address = live_market.neg_risk ? NEG_RISK_CTF_EXCHANGE : CTF_EXCHANGE;
        std::cout << "    Exchange: " << exchange_address << "\n";

        if (!have_credentials)
        {
            std::cerr << "    Live mode requires API credentials\n";
            return false;
        }

        constexpr double order_usd = 1.0;
        const SignatureType live_signature_type = (funder_address != signer.address())
                                                      ? SignatureType::POLY_GNOSIS_SAFE
                                                      : SignatureType::EOA;
        ClobClient order_client(CLOB_API, 137, private_key, credentials, live_signature_type, funder_address);

        CreateMarketOrderParams market_order;
        market_order.token_id = live_market.token_id;
        market_order.amount = order_usd;
        market_order.side = OrderSide::BUY;
        market_order.price = live_market.best_ask;
        market_order.tick_size = live_market.tick_size;
        market_order.neg_risk = live_market.neg_risk;

        std::cout << "    Placing FAK market order: $" << order_usd << "\n";

        const auto prepared = order_client.create_market_order(
            market_order, OrderType::FAK);
        const auto &real_signed = prepared.order;

        std::cout << "    Order data for signing:\n";
        std::cout << "      maker: " << real_signed.maker << "\n";
        std::cout << "      signer: " << real_signed.signer << "\n";
        std::cout << "      taker: " << real_signed.taker << "\n";
        std::cout << "      tokenId: " << real_signed.token_id << "\n";
        std::cout << "      makerAmount: " << real_signed.maker_amount << "\n";
        std::cout << "      takerAmount: " << real_signed.taker_amount << "\n";
        std::cout << "      side: " << static_cast<int>(real_signed.side) << "\n";
        std::cout << "      signatureType: " << static_cast<int>(real_signed.signature_type) << "\n";
        std::cout << "      exchange: " << exchange_address << "\n";

        const auto post_response = order_client.post_order(prepared);
        if (post_response.success)
        {
            std::cout << "\n    ✅ ORDER PLACED SUCCESSFULLY!\n";
            std::cout << "    Order ID: " << post_response.order_id << "\n";
            std::cout << "    Status: " << post_response.status << "\n";
            std::cout << "    Cost: $" << post_response.making_amount << "\n";
            std::cout << "    Shares: " << post_response.taking_amount << "\n";
        }
        else
        {
            std::cerr << "\n    Order placement failed: " << post_response.error_msg << "\n";
        }

        return true;
    }
}
