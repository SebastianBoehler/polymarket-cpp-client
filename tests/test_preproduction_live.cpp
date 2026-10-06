#include "check_support.hpp"
#include "polymarket/clob_client.hpp"
#include "polymarket/environment.hpp"
#include <cstdlib>
#include <iostream>
#include <string_view>

// Read-only smoke test against the preproduction CLOB. It sends no orders and
// needs no credentials. Preproduction runs on Polygon mainnet, so this test
// must stay read-only.
int main()
{
    using check_support::check;
    using namespace polymarket;

    const char *run_live = std::getenv("POLYMARKET_RUN_LIVE_SMOKE");
    if (!run_live || std::string_view(run_live) != "1")
    {
        std::cout << "test_preproduction_live skipped: set "
                     "POLYMARKET_RUN_LIVE_SMOKE=1 to enable live preproduction calls\n";
        return 0;
    }

    http_global_init();
    {
        ClobClient client(Environment::preproduction());
        const auto server_time = client.get_server_time();
        check(server_time && *server_time > 0, "preproduction CLOB must report its server time");

        const auto markets = client.get_markets();
        check(!markets.data.empty(), "preproduction CLOB must list markets");
        if (!markets.data.empty())
            check(!markets.data.front().condition_id.empty(),
                  "preproduction markets must carry condition IDs");
    }
    http_global_cleanup();
    return check_support::finish("test_preproduction_live");
}
