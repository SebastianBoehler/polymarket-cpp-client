#include "clob_client.hpp"
#include <iostream>
#include <cstdlib>

// Helper to get env var or empty string
static std::string get_env(const char *name)
{
    const char *val = std::getenv(name);
    return val ? val : "";
}

int main()
{
    using namespace polymarket;

    try
    {
        http_global_init();

        // ============================================================
        // PUBLIC ENDPOINTS (No authentication required)
        // ============================================================

        std::cout << "=== Public Endpoints ===\n\n";

        ClobClient public_client{"https://clob.polymarket.com", 137};

        // Get markets
        auto markets = public_client.get_markets();
        std::cout << "Fetched markets: " << markets.size() << "\n";

        std::string example_token_id;
        std::string example_condition_id;
        bool example_neg_risk = false;

        if (!markets.empty())
        {
            const auto &m = markets.front();
            std::cout << "First market: " << m.market_slug << " (" << m.condition_id << ")\n";
            example_condition_id = m.condition_id;
            example_neg_risk = m.neg_risk;

            if (!m.tokens.empty())
            {
                example_token_id = m.tokens[0].token_id;
                std::cout << "  Token ID: " << example_token_id << "\n";
            }
        }

        // Get orderbook
        if (!example_token_id.empty())
        {
            auto book = public_client.get_order_book(example_token_id);
            if (book)
            {
                std::cout << "\nOrderbook for token:\n";
                std::cout << "  Bids: " << book->bids.size() << " levels\n";
                std::cout << "  Asks: " << book->asks.size() << " levels\n";
                if (!book->bids.empty())
                {
                    std::cout << "  Best bid: " << book->bids[0].price << " @ " << book->bids[0].size << "\n";
                }
                if (!book->asks.empty())
                {
                    std::cout << "  Best ask: " << book->asks[0].price << " @ " << book->asks[0].size << "\n";
                }
            }

            // Get price
            auto price = public_client.get_price(example_token_id, "buy");
            if (price)
            {
                std::cout << "\nBuy price: " << price->price << "\n";
            }

            // Get midpoint
            auto mid = public_client.get_midpoint(example_token_id);
            if (mid)
            {
                std::cout << "Midpoint: " << mid->mid << "\n";
            }

            // Get spread
            auto spread = public_client.get_spread(example_token_id);
            if (spread)
            {
                std::cout << "Spread: " << spread->spread << "\n";
            }
        }

        // ============================================================
        // AUTHENTICATED ENDPOINTS (Requires API credentials)
        // ============================================================

        std::string private_key = get_env("POLY_PRIVATE_KEY");
        std::string api_key = get_env("POLY_API_KEY");
        std::string api_secret = get_env("POLY_API_SECRET");
        std::string api_passphrase = get_env("POLY_API_PASSPHRASE");
        std::string funder_address = get_env("POLY_FUNDER_ADDRESS");

        if (private_key.empty() || api_key.empty())
        {
            std::cout << "\n=== Authenticated Endpoints ===\n";
            std::cout << "Skipping (set POLY_PRIVATE_KEY, POLY_API_KEY, POLY_API_SECRET, POLY_API_PASSPHRASE)\n";
            http_global_cleanup();
            return 0;
        }

        std::cout << "\n=== Authenticated Endpoints ===\n\n";

        ApiCredentials creds;
        creds.api_key = api_key;
        creds.api_secret = api_secret;
        creds.api_passphrase = api_passphrase;

        // Determine signature type based on funder address
        SignatureType sig_type = funder_address.empty() ? SignatureType::EOA : SignatureType::POLY_PROXY;

        ClobClient client{
            "https://clob.polymarket.com",
            137,
            private_key,
            creds,
            sig_type,
            funder_address};

        // Get balance
        auto balance = client.get_balance_allowance("USDC");
        if (balance)
        {
            std::cout << "USDC Balance: " << balance->balance << "\n";
            std::cout << "USDC Allowance: " << balance->allowance << "\n";
        }

        // Get open orders
        auto open_orders = client.get_open_orders();
        std::cout << "\nOpen orders: " << open_orders.size() << "\n";
        for (const auto &order : open_orders)
        {
            std::cout << "  Order " << order.id << ": " << order.side << " " << order.original_size
                      << " @ " << order.price << " (" << order.status << ")\n";
        }

        // ============================================================
        // GET POSITIONS (Data API)
        // ============================================================

        std::cout << "\n=== Positions ===\n\n";

        auto positions = client.get_positions();
        std::cout << "Positions: " << positions.size() << "\n";
        for (const auto &pos : positions)
        {
            std::cout << "  " << pos.title << " (" << pos.outcome << ")\n";
            std::cout << "    Size: " << pos.size << " shares\n";
            std::cout << "    Avg price: " << pos.avg_price << "\n";
            std::cout << "    Current value: $" << pos.current_value << "\n";
            std::cout << "    P&L: $" << pos.cash_pnl << " (" << pos.percent_pnl << "%)\n";
        }

        // ============================================================
        // CREATE ORDER EXAMPLE (does NOT post to exchange)
        // ============================================================

        std::cout << "\n=== Create Order Example ===\n\n";

        if (!example_token_id.empty())
        {
            // Create a limit order (not posted)
            CreateOrderParams order_params;
            order_params.token_id = example_token_id;
            order_params.price = 0.50;
            order_params.size = 10.0;
            order_params.side = OrderSide::BUY;
            order_params.neg_risk = example_neg_risk; // Use cached neg_risk to skip API call

            auto signed_order = client.create_order(order_params);
            std::cout << "Created signed order:\n";
            std::cout << "  Maker: " << signed_order.maker << "\n";
            std::cout << "  Token ID: " << signed_order.token_id << "\n";
            std::cout << "  Maker amount: " << signed_order.maker_amount << "\n";
            std::cout << "  Taker amount: " << signed_order.taker_amount << "\n";
            std::cout << "  Side: " << (signed_order.side == 0 ? "BUY" : "SELL") << "\n";
            std::cout << "  Signature: " << signed_order.signature.substr(0, 20) << "...\n";

            // To actually post the order:
            // auto response = client.post_order(signed_order, OrderType::GTC);

            // Or use the combined method:
            // auto response = client.create_and_post_order(order_params, OrderType::GTC);
        }

        // ============================================================
        // BATCH ORDER EXAMPLE (does NOT post to exchange)
        // ============================================================

        std::cout << "\n=== Batch Order Example ===\n\n";

        if (!example_token_id.empty())
        {
            std::vector<BatchOrderEntry> batch_orders;

            // Create multiple orders at different price levels
            for (int i = 0; i < 3; ++i)
            {
                CreateOrderParams params;
                params.token_id = example_token_id;
                params.price = 0.45 + (i * 0.02); // 0.45, 0.47, 0.49
                params.size = 5.0;
                params.side = OrderSide::BUY;
                params.neg_risk = example_neg_risk;

                auto signed_order = client.create_order(params);

                BatchOrderEntry entry;
                entry.order = signed_order;
                entry.order_type = OrderType::GTC;
                batch_orders.push_back(entry);

                std::cout << "Batch order " << (i + 1) << ": BUY 5 @ " << params.price << "\n";
            }

            std::cout << "\nCreated " << batch_orders.size() << " batch orders (not posted)\n";

            // To actually post the batch:
            // auto responses = client.post_orders(batch_orders);
            // for (const auto& resp : responses) {
            //     if (resp.success) {
            //         std::cout << "Order " << resp.order_id << " posted successfully\n";
            //     } else {
            //         std::cout << "Order failed: " << resp.error_msg << "\n";
            //     }
            // }
        }

        // ============================================================
        // CANCEL ORDER EXAMPLE
        // ============================================================

        std::cout << "\n=== Cancel Order Example ===\n\n";

        // Cancel a specific order by ID:
        // bool cancelled = client.cancel_order("order-id-here");

        // Cancel multiple orders:
        // bool cancelled = client.cancel_orders({"order-id-1", "order-id-2"});

        // Cancel all orders:
        // bool cancelled = client.cancel_all();

        // Cancel all orders for a specific market:
        // bool cancelled = client.cancel_market_orders(example_condition_id);

        std::cout << "Cancel examples (commented out to avoid accidental cancellation)\n";

        http_global_cleanup();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
