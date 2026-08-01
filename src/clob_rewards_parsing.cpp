#include "clob_client_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace polymarket::detail
{
    namespace
    {
        void require_object(const json &item, const char *name)
        {
            if (!item.is_object())
                throw std::invalid_argument(std::string(name) + " must be an object");
        }

        std::string required_string(const json &item, const char *field,
                                    bool allow_empty = false)
        {
            const auto &value = item.at(field);
            if (!value.is_string())
                throw std::invalid_argument(std::string(field) + " must be a string");
            auto result = value.get<std::string>();
            if (!allow_empty && result.empty())
                throw std::invalid_argument(std::string(field) + " must not be empty");
            return result;
        }

        std::string numeric_string(const json &item, const char *field)
        {
            const auto &value = item.at(field);
            (void)strict_json_number(value);
            return value.is_string() ? value.get<std::string>() : value.dump();
        }

        std::string integer_string(const json &value, const char *field)
        {
            if (value.is_number_unsigned())
                return value.dump();
            if (value.is_number_integer())
            {
                if (value.get<int64_t>() < 0)
                    throw std::invalid_argument(std::string(field) + " must be nonnegative");
                return value.dump();
            }
            if (!value.is_string())
                throw std::invalid_argument(std::string(field) + " must be an integer or string");
            const auto &text = value.get_ref<const std::string &>();
            if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c)
                                             { return std::isdigit(c) != 0; }))
                throw std::invalid_argument(std::string(field) + " must be a nonnegative integer");
            return text;
        }

        RewardsConfig parse_rewards_config(const json &item, bool detailed)
        {
            require_object(item, "rewards_config item");
            RewardsConfig config;
            config.asset_address = required_string(item, "asset_address");
            config.start_date = required_string(item, "start_date");
            config.end_date = required_string(item, "end_date");
            config.rate_per_day = numeric_string(item, "rate_per_day");
            config.total_rewards = numeric_string(item, "total_rewards");

            if (detailed)
            {
                config.id = integer_string(item.at("id"), "id");
                config.total_days = numeric_string(item, "total_days");
            }
            else
            {
                if (item.contains("id"))
                    config.id = integer_string(item.at("id"), "id");
                if (item.contains("total_days"))
                    config.total_days = numeric_string(item, "total_days");
            }
            return config;
        }

        std::vector<RewardsConfig> parse_rewards_configs(const json &item,
                                                         bool detailed)
        {
            const auto &configs = item.at("rewards_config");
            if (!configs.is_array())
                throw std::invalid_argument("rewards_config must be an array");
            std::vector<RewardsConfig> result;
            result.reserve(configs.size());
            for (const auto &config : configs)
                result.push_back(parse_rewards_config(config, detailed));
            return result;
        }

        RewardToken parse_reward_token(const json &item)
        {
            require_object(item, "reward token");
            RewardToken token;
            token.token_id = required_string(item, "token_id");
            token.outcome = required_string(item, "outcome");
            const auto &price = item.at("price");
            (void)json_probability(price);
            token.price = price.is_string() ? price.get<std::string>() : price.dump();
            return token;
        }

        std::vector<RewardToken> parse_reward_tokens(const json &item)
        {
            const auto &tokens = item.at("tokens");
            if (!tokens.is_array())
                throw std::invalid_argument("tokens must be an array");
            std::vector<RewardToken> result;
            result.reserve(tokens.size());
            for (const auto &token : tokens)
                result.push_back(parse_reward_token(token));
            return result;
        }

        EarningsInfo parse_earning(const json &item, bool require_condition)
        {
            require_object(item, "earning");
            EarningsInfo info;
            info.date = required_string(item, "date");
            if (require_condition)
                info.condition_id = required_string(item, "condition_id");
            info.asset_address = required_string(item, "asset_address");
            info.maker_address = required_string(item, "maker_address");
            info.earnings = numeric_string(item, "earnings");
            info.asset_rate = numeric_string(item, "asset_rate");
            return info;
        }
    }

    ClobClient::RewardsInfo parse_current_reward_info(const json &item)
    {
        require_object(item, "current reward");
        RewardsInfo info;
        info.condition_id = required_string(item, "condition_id");
        info.rewards_config = parse_rewards_configs(item, false);
        info.rewards_max_spread = numeric_string(item, "rewards_max_spread");
        info.rewards_min_size = numeric_string(item, "rewards_min_size");
        return info;
    }

    ClobClient::RewardsInfo parse_market_reward_info(const json &item)
    {
        require_object(item, "market reward");
        RewardsInfo info;
        info.condition_id = required_string(item, "condition_id");
        info.question = required_string(item, "question");
        info.market_slug = required_string(item, "market_slug");
        info.event_slug = required_string(item, "event_slug");
        info.image = required_string(item, "image", true);
        info.rewards_max_spread = numeric_string(item, "rewards_max_spread");
        info.rewards_min_size = numeric_string(item, "rewards_min_size");
        info.market_competitiveness = numeric_string(item, "market_competitiveness");
        info.tokens = parse_reward_tokens(item);
        info.rewards_config = parse_rewards_configs(item, true);
        return info;
    }

    ClobClient::EarningsInfo parse_user_earning_info(const json &item)
    {
        return parse_earning(item, true);
    }

    ClobClient::EarningsInfo parse_total_user_earning_info(const json &item)
    {
        return parse_earning(item, false);
    }
}
