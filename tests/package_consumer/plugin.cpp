#include <http_client.hpp>

extern "C" int polymarket_package_plugin_probe()
{
    polymarket::HttpClient client;
    return client.options().timeout_ms > 0 ? 0 : 1;
}
