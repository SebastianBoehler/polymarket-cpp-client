#include "clob_client.hpp"
#include "clob_positions_internal.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace
{
    using json = nlohmann::json;
    using namespace polymarket;

    json position_item(size_t index)
    {
        return {
            {"proxyWallet", "0x1111111111111111111111111111111111111111"},
            {"asset", "asset-" + std::to_string(index)},
            {"conditionId", "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
            {"size", 12.5}, {"avgPrice", 0.4}, {"initialValue", 5.0},
            {"currentValue", 7.5}, {"cashPnl", 2.5}, {"percentPnl", 50.0},
            {"totalBought", 20.0}, {"realizedPnl", 1.25},
            {"percentRealizedPnl", 6.25}, {"curPrice", 0.6},
            {"redeemable", true}, {"mergeable", false}, {"title", "Title"},
            {"slug", "market-slug"}, {"icon", "https://example.test/icon.png"},
            {"eventSlug", "event-slug"}, {"outcome", "Yes"}, {"outcomeIndex", 0},
            {"oppositeOutcome", "No"}, {"oppositeAsset", "opposite-token"},
            {"endDate", "2026-08-01T00:00:00Z"}, {"negativeRisk", false}};
    }

    HttpResponse response_body(std::string body, long status = 200)
    {
        HttpResponse result;
        result.status_code = status;
        result.body = std::move(body);
        return result;
    }

    HttpResponse response(json body, long status = 200)
    {
        return response_body(body.dump(), status);
    }

    bool check(bool condition, const char *message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    bool test_pagination_and_current_fields()
    {
        std::vector<std::string> paths;
        size_t call = 0;
        const auto positions = detail::fetch_position_pages(
            "0xabc+/=", detail::PositionFilter::all,
            [&](const std::string &path)
            {
                paths.push_back(path);
                json page = json::array();
                const size_t count = call++ == 0 ? 500 : 1;
                for (size_t i = 0; i < count; ++i)
                    page.push_back(position_item(i + (count == 1 ? 500 : 0)));
                return response(std::move(page));
            });

        return check(paths.size() == 2, "positions must fetch a short terminal page") &&
               check(paths[0] == "/positions?user=0xabc%2B%2F%3D&limit=500&offset=0" &&
                         paths[1] == "/positions?user=0xabc%2B%2F%3D&limit=500&offset=500",
                     "positions pagination query mismatch") &&
               check(positions.size() == 501 && positions.back().asset == "asset-500",
                     "positions pages must be combined") &&
               check(positions[0].total_bought == 20.0 &&
                         positions[0].realized_pnl == 1.25 &&
                         positions[0].percent_realized_pnl == 6.25 &&
                         positions[0].icon == "https://example.test/icon.png" &&
                         positions[0].event_slug == "event-slug" &&
                         positions[0].opposite_outcome == "No",
                     "current position fields must be retained");
    }

    bool test_native_filters()
    {
        std::vector<std::string> paths;
        const auto fetch = [&](const std::string &path)
        {
            paths.push_back(path);
            return response(json::array());
        };
        (void)detail::fetch_position_pages("0xuser", detail::PositionFilter::redeemable, fetch);
        (void)detail::fetch_position_pages("0xuser", detail::PositionFilter::mergeable, fetch);
        return check(paths.size() == 2, "position filters must each issue one request") &&
               check(paths[0] == "/positions?user=0xuser&redeemable=true&limit=500&offset=0",
                     "redeemable positions must use the native query filter") &&
               check(paths[1] == "/positions?user=0xuser&mergeable=true&limit=500&offset=0",
                     "mergeable positions must use the native query filter");
    }

    bool test_atomic_malformed_page()
    {
        size_t call = 0;
        const auto positions = detail::fetch_position_pages(
            "0xuser", detail::PositionFilter::all,
            [&](const std::string &)
            {
                json page = json::array();
                if (call++ == 0)
                {
                    for (size_t i = 0; i < 500; ++i)
                        page.push_back(position_item(i));
                }
                else
                {
                    auto malformed = position_item(500);
                    malformed["icon"] = json::object();
                    page.push_back(std::move(malformed));
                }
                return response(std::move(page));
            });
        return check(positions.empty(),
                     "a malformed later positions page must invalidate the whole result");
    }

    bool test_all_current_fields_are_required()
    {
        const std::vector<const char *> fields{
            "proxyWallet", "asset", "conditionId", "size", "avgPrice",
            "initialValue", "currentValue", "cashPnl", "percentPnl",
            "totalBought", "realizedPnl", "percentRealizedPnl", "curPrice",
            "redeemable", "mergeable", "title", "slug", "icon", "eventSlug",
            "outcome", "outcomeIndex", "oppositeOutcome", "oppositeAsset",
            "endDate", "negativeRisk"};
        for (const auto *field : fields)
        {
            auto malformed = position_item(0);
            malformed.erase(field);
            const auto positions = detail::fetch_position_pages(
                "0xuser", detail::PositionFilter::all,
                [&](const std::string &)
                {
                    return response(json::array({malformed}));
                });
            if (!positions.empty())
                return check(false, "positions must require every current schema field");
        }
        return true;
    }

    bool rejects_position(json item)
    {
        const auto positions = detail::fetch_position_pages(
            "0xuser", detail::PositionFilter::all,
            [&](const std::string &)
            {
                return response(json::array({std::move(item)}));
            });
        return positions.empty();
    }

    bool test_current_field_types_are_strict()
    {
        auto numeric_string = position_item(0);
        numeric_string["totalBought"] = "20";
        auto numeric_bool = position_item(0);
        numeric_bool["redeemable"] = 1;
        auto fractional_index = position_item(0);
        fractional_index["outcomeIndex"] = 0.5;
        auto object_title = position_item(0);
        object_title["title"] = json::object();
        return check(rejects_position(std::move(numeric_string)),
                     "position numbers must be JSON numbers") &&
               check(rejects_position(std::move(numeric_bool)),
                     "position booleans must be JSON booleans") &&
               check(rejects_position(std::move(fractional_index)),
                     "position outcomeIndex must be an integer") &&
               check(rejects_position(std::move(object_title)),
                     "position text fields must be strings");
    }

    bool test_documented_offset_bound()
    {
        json full_page = json::array();
        for (size_t i = 0; i < 500; ++i)
            full_page.push_back(position_item(i));
        const std::string body = full_page.dump();
        std::vector<std::string> paths;
        const auto positions = detail::fetch_position_pages(
            "0xuser", detail::PositionFilter::all,
            [&](const std::string &path)
            {
                paths.push_back(path);
                return response_body(body);
            });
        return check(paths.size() == 21 && positions.size() == 10'500,
                     "positions pagination must stop at the documented max offset") &&
               check(paths.back() == "/positions?user=0xuser&limit=500&offset=10000",
                     "positions pagination must never request an offset above 10000");
    }
}

int main(int argc, char **argv)
{
    if (argc == 3 && std::string(argv[1]) == "--live")
    {
        ClobClient client;
        const auto positions = client.get_positions(argv[2]);
        return check(!positions.empty(), "live Data API positions response must parse")
                   ? 0
                   : 1;
    }
    return test_pagination_and_current_fields() && test_native_filters() &&
                   test_atomic_malformed_page() && test_all_current_fields_are_required() &&
                   test_current_field_types_are_strict() && test_documented_offset_bound()
               ? 0
               : 1;
}
