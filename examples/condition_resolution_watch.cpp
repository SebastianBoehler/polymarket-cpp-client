#include "evm_watch_common.hpp"

int main(int argc, char **argv)
{
    return polymarket_example::run_watch(
        argc,
        argv,
        "--ctf",
        "POLYMARKET_CONDITIONAL_TOKENS",
        [](const std::string &address)
        { return polymarket::conditional_tokens_log_filter(address); });
}
