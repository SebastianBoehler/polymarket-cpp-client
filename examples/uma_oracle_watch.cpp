#include "evm_watch_common.hpp"

int main(int argc, char **argv)
{
    return polymarket_example::run_watch(
        argc,
        argv,
        "--adapter",
        "POLYMARKET_UMA_CTF_ADAPTER",
        [](const std::string &address)
        { return polymarket::uma_adapter_log_filter(address); });
}
