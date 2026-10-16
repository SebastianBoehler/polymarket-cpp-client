#pragma once

#include "polymarket/gamma_client.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Gamma request paths and query strings. Invalid input throws
// std::invalid_argument.
namespace polymarket::detail
{
    class GammaQuery
    {
      public:
        void add(std::string_view key, const std::optional<bool> &value);
        void add(std::string_view key, const std::optional<double> &value);
        void add(std::string_view key, const std::optional<std::int64_t> &value);
        void add(std::string_view key, const std::string &value); // skipped when empty
        void add(std::string_view key, const std::vector<std::string> &values);
        void add(std::string_view key, const std::vector<std::int64_t> &values);
        // "?a=1&b=2", or "" when nothing was added.
        std::string str() const;

      private:
        void append(std::string_view key, std::string_view value);
        std::string query_;
    };

    std::string gamma_market_list_query(const GammaMarketFilter &filter);
    std::string gamma_event_list_query(const GammaEventFilter &filter);
    std::string gamma_market_lookup_query(const GammaMarketLookupOptions &options);
    std::string gamma_event_lookup_query(const GammaEventLookupOptions &options);

    // "/<kind>s/<id>" or "/<kind>s/slug/<slug>", each segment percent-encoded.
    std::string gamma_id_path(std::string_view kind, const std::string &id);
    std::string gamma_slug_path(std::string_view kind, const std::string &slug);
    // Slug from a https://polymarket.com URL, as py-sdk gamma_paths.py reads it.
    std::string gamma_slug_from_url(std::string_view kind, const std::string &url);
} // namespace polymarket::detail
