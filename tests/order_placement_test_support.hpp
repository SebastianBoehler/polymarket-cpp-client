#pragma once

#include "check_support.hpp"
#include "polymarket/clob_client.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Metadata failures shared by the limit and market placement tests.
namespace order_placement_test
{
    enum class MetadataBranch : std::uint8_t
    {
        TickRefresh,
        NegRisk
    };

    struct MetadataFailure
    {
        int status;
        const char *body;
        const char *name;
    };

    inline const std::vector<MetadataBranch> metadata_branches = {MetadataBranch::TickRefresh,
                                                                  MetadataBranch::NegRisk};
    inline const std::vector<MetadataFailure> metadata_failures = {
        {404, R"({"error":"market not found"})", "404"}, {200, "not json", "malformed 200"}};

    inline std::string branch_name(MetadataBranch branch)
    {
        switch (branch)
        {
        case MetadataBranch::TickRefresh:
            return "tick refresh";
        case MetadataBranch::NegRisk:
            return "neg-risk";
        }
        return "unknown";
    }

    inline std::string failed_endpoint(MetadataBranch branch)
    {
        return branch == MetadataBranch::NegRisk ? "/neg-risk" : "/tick-size";
    }

    inline void check_preserved_error(const polymarket::Result<polymarket::OrderResponse> &result,
                                      MetadataBranch branch, const MetadataFailure &failure,
                                      const std::string &helper)
    {
        using polymarket::SdkErrorCode;
        const auto label = helper + " " + branch_name(branch) + " " + failure.name;
        if (result)
        {
            check_support::check(false, label + ": the order must not be placed");
            return;
        }
        const auto &error = result.error();
        check_support::check(error.endpoint == failed_endpoint(branch),
                             label + ": the error must name the failed lookup");
        if (failure.status == 404)
            check_support::check(
                error.code == SdkErrorCode::ApiResponse && error.http_status == 404 &&
                    !error.retryable &&
                    error.response_body_excerpt.find("market not found") != std::string::npos,
                label + ": a 404 must stay a non-retryable API rejection with its body");
        else
            check_support::check(error.code == SdkErrorCode::Parse && !error.retryable,
                                 label + ": a malformed body must stay a parse failure");
    }
} // namespace order_placement_test
