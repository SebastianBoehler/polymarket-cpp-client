#include "polymarket/geoblock.hpp"

#include <nlohmann/json.hpp>

namespace polymarket
{
    namespace
    {
        constexpr const char *geoblock_path = "/api/geoblock";

        std::string optional_string(const nlohmann::json &body, const char *key)
        {
            const auto field = body.find(key);
            return field != body.end() && field->is_string() ? field->get<std::string>() : "";
        }
    } // namespace

    Result<GeoblockStatus> check_geoblock(const HttpClientOptions &options,
                                          const std::string &base_url)
    {
        HttpClient http(options);
        http.set_base_url(base_url);
        return check_geoblock(http);
    }

    Result<GeoblockStatus> check_geoblock(HttpClient &http)
    {
        const auto response = http.get(geoblock_path);
        if (!response.ok())
            return Result<GeoblockStatus>::failure(make_sdk_error(response, geoblock_path));

        try
        {
            const auto body = nlohmann::json::parse(response.body);
            const auto blocked = body.find("blocked");
            // Fail closed: an answer without a boolean verdict is not "allowed".
            if (blocked == body.end() || !blocked->is_boolean())
                return Result<GeoblockStatus>::failure(
                    make_parse_error("geoblock response requires a boolean blocked field",
                                     geoblock_path, response.body));
            return Result<GeoblockStatus>::success(
                {blocked->get<bool>(), optional_string(body, "ip"),
                 optional_string(body, "country"), optional_string(body, "region")});
        }
        catch (const nlohmann::json::exception &error)
        {
            return Result<GeoblockStatus>::failure(
                make_parse_error(error.what(), geoblock_path, response.body));
        }
    }
} // namespace polymarket
