// PositionClient EOA trading approval setup failures: before the first
// submission, after partial progress, and after a mined revert.
#include "trading_approvals_test_support.hpp"

using namespace approvals_test;

namespace
{
    void eoa_setup_failures()
    {
        const auto approve = detail::erc20_approve_call(kContracts.collateral_token,
                                                        kContracts.neg_risk_exchange, max_uint256);

        // Before the first submission: the original error, nothing sent.
        {
            clob_test::LocalServer node;
            PositionClient client(base_config(node.url()));
            expect_two_missing(node);
            expect_rpc(node, "eth_chainId", "0x89");
            expect_rpc(node, "eth_getTransactionCount", "0x7");
            expect_rpc(node, "eth_gasPrice", "0x6fc23ac00");
            expect_rpc_error(node, "eth_estimateGas");
            try
            {
                (void)client.setup_trading_approvals(std::chrono::seconds(5));
                check(false, "first approval failure did not throw");
            }
            catch (const PartialBatchError &)
            {
                check(false, "nothing was sent, so the error is not partial");
            }
            catch (const std::runtime_error &)
            {
            }
            check(count(node, "eth_sendRawTransaction") == 0,
                  "failed first approval sends nothing");
        }

        // After partial progress: the mined approve's hash survives.
        {
            clob_test::LocalServer node;
            PositionClient client(base_config(node.url()));
            expect_two_missing(node);
            expect_transaction(node, approve, true);
            expect_mined(node);
            expect_rpc(node, "eth_getTransactionCount", "0x8");
            expect_rpc(node, "eth_gasPrice", "0x6fc23ac00");
            expect_rpc_error(node, "eth_estimateGas");
            try
            {
                (void)client.setup_trading_approvals(std::chrono::seconds(5));
                check(false, "second approval failure did not throw");
            }
            catch (const PartialBatchError &error)
            {
                check(error.submitted_hashes().size() == 1 && error.failed_call_index() == 1 &&
                          error.cause() != nullptr,
                      "partial setup keeps the first hash and the failing index");
            }
            check(count(node, "eth_sendRawTransaction") == 1, "operator approval was not sent");
        }

        // After a mined revert: the reverted approve stops the batch.
        {
            clob_test::LocalServer node;
            PositionClient client(base_config(node.url()));
            expect_two_missing(node);
            expect_transaction(node, approve, true);
            expect_rpc(node, "eth_getTransactionReceipt", [](const nlohmann::json &params)
                       { return receipt(params.at(0).get<std::string>(), false); });
            try
            {
                (void)client.setup_trading_approvals(std::chrono::seconds(5));
                check(false, "reverted approval did not throw");
            }
            catch (const PartialBatchError &error)
            {
                bool reverted = false;
                try
                {
                    std::rethrow_exception(error.cause());
                }
                catch (const TransactionRevertedError &)
                {
                    reverted = true;
                }
                catch (...)
                {
                }
                check(error.submitted_hashes().size() == 1 && error.failed_call_index() == 0 &&
                          reverted,
                      "reverted approve is reported with its hash and cause");
            }
            check(count(node, "eth_sendRawTransaction") == 1, "nothing sent after the revert");
        }
    }
} // namespace

int main()
{
    eoa_setup_failures();
    return check_support::finish("test_trading_approvals_failures");
}
