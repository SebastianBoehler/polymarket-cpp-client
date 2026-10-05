// Receipt and relayer waiting with a timeout shorter than the poll interval.
#include "transaction_waiters.hpp"
#include "position_test_support.hpp"

using namespace position_test;

namespace
{
    using Clock = std::chrono::steady_clock;

    const std::string kHash = "0x" + std::string(64, 'a');
    const auto kTimeout = std::chrono::milliseconds(300);
    const auto kDefaultPoll = std::chrono::seconds(2);
    // Well under the poll interval: the wait must not sleep a full interval.
    const auto kSlack = std::chrono::milliseconds(1500);

    long long elapsed_ms(Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    nlohmann::json relayer_tx(const std::string &state, const std::string &hash)
    {
        return {{"transaction_id", "tx-1"}, {"transaction_hash", hash}, {"state", state}, {"error_msg", nullptr}};
    }

    struct Servers
    {
        clob_test::LocalServer node;
        clob_test::LocalServer relayer_server;
        EvmJsonRpcHttpClient rpc{node.url()};
        detail::RelayerClient relayer{relayer_server.url(), "relayer-key", kWallet};

        Servers() { rpc.set_timeout_ms(5000); }
    };

    void receipt_mined_before_short_deadline()
    {
        Servers s;
        expect_rpc(s.node, "eth_getTransactionReceipt", nlohmann::json());
        expect_rpc(s.node, "eth_getTransactionReceipt", receipt(kHash, true));
        const auto start = Clock::now();
        const auto outcome = detail::wait_for_receipt(s.rpc, kHash, kTimeout, kDefaultPoll);
        check(outcome.receipt.success, "receipt found at the deadline poll");
        check(s.node.requests().size() == 2, "polled again before timing out");
        check(Clock::now() - start < kSlack, "slept at most the timeout: " + std::to_string(elapsed_ms(start)) + " ms");
    }

    void receipt_times_out_at_deadline()
    {
        Servers s;
        expect_rpc(s.node, "eth_getTransactionReceipt", nlohmann::json());
        expect_rpc(s.node, "eth_getTransactionReceipt", nlohmann::json());
        const auto start = Clock::now();
        expect_throws<TransactionTimeoutError>("receipt timeout", [&]
                                               { (void)detail::wait_for_receipt(s.rpc, kHash, kTimeout, kDefaultPoll); });
        const auto waited = Clock::now() - start;
        check(waited >= kTimeout && waited < kSlack,
              "timed out at the deadline, not before: " + std::to_string(elapsed_ms(start)) + " ms");
        check(s.node.requests().size() == 2, "one poll at the start and one at the deadline");
    }

    void relayer_confirmed_before_short_deadline()
    {
        Servers s;
        s.relayer_server.enqueue(relayer_tx("STATE_NEW", "").dump());
        s.relayer_server.enqueue(relayer_tx("STATE_CONFIRMED", kHash).dump());
        expect_rpc(s.node, "eth_getTransactionReceipt", receipt(kHash, true));
        const auto start = Clock::now();
        const auto outcome = detail::wait_for_relayer(s.relayer, s.rpc, "tx-1", "", kTimeout, kDefaultPoll);
        check(outcome.transaction_hash == kHash && outcome.transaction_id == "tx-1", "relayer outcome");
        check(s.relayer_server.requests().size() == 2, "relayer polled again before timing out");
        check(Clock::now() - start < kSlack, "slept at most the timeout: " + std::to_string(elapsed_ms(start)) + " ms");
    }

    void relayer_times_out_at_deadline()
    {
        Servers s;
        s.relayer_server.enqueue(relayer_tx("STATE_NEW", "").dump());
        s.relayer_server.enqueue(relayer_tx("STATE_NEW", "").dump());
        const auto start = Clock::now();
        expect_throws<TransactionTimeoutError>("relayer timeout", [&]
                                               { (void)detail::wait_for_relayer(s.relayer, s.rpc, "tx-1", "", kTimeout,
                                                                                kDefaultPoll); });
        const auto waited = Clock::now() - start;
        check(waited >= kTimeout && waited < kSlack,
              "timed out at the deadline, not before: " + std::to_string(elapsed_ms(start)) + " ms");
        check(s.relayer_server.requests().size() == 2, "one poll at the start and one at the deadline");
    }
} // namespace

int main()
{
    receipt_mined_before_short_deadline();
    receipt_times_out_at_deadline();
    relayer_confirmed_before_short_deadline();
    relayer_times_out_at_deadline();
    return check_support::finish("test_transaction_waiters");
}
