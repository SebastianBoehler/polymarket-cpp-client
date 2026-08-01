#include "clob_client_internal.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace polymarket::detail
{
    namespace
    {
        const json &required_field(const json &object, const char *name)
        {
            if (!object.contains(name))
                throw std::invalid_argument(std::string("response is missing ") + name);
            return object.at(name);
        }

        std::string required_string(const json &object, const char *name)
        {
            const auto &value = required_field(object, name);
            if (!value.is_string())
                throw std::invalid_argument(std::string("response field ") + name +
                                            " must be a string");
            return value.get<std::string>();
        }

        void optional_string(const json &object, const char *name, std::string &target)
        {
            if (!object.contains(name)) return;
            if (!object.at(name).is_string())
                throw std::invalid_argument(std::string("response field ") + name +
                                            " must be a string");
            target = object.at(name).get<std::string>();
        }

        const json *optional_defaulted_field(const json &object,
                                             const char *name,
                                             const char *alias = nullptr)
        {
            const bool has_name = object.contains(name);
            const bool has_alias = alias && object.contains(alias);
            if (has_name && has_alias)
                throw std::invalid_argument(std::string("response contains both ") +
                                            name + " and " + alias);
            if (!has_name && !has_alias) return nullptr;

            const auto &value = object.at(has_name ? name : alias);
            return value.is_null() ? nullptr : &value;
        }

        void optional_string_array(const json &object,
                                   const char *name,
                                   const char *alias,
                                   std::vector<std::string> &target)
        {
            const auto *values = optional_defaulted_field(object, name, alias);
            if (!values) return;
            if (!values->is_array())
                throw std::invalid_argument(std::string("response field ") + name +
                                            " must be an array");
            for (const auto &value : *values)
            {
                if (!value.is_string())
                    throw std::invalid_argument(std::string(name) +
                                                " entries must be strings");
                target.push_back(value.get<std::string>());
            }
        }
    }

    OrderResponse parse_order_response_json_strict(const std::string &json_text)
    {
        const auto parsed = json::parse(json_text);
        if (!parsed.is_object())
            throw std::invalid_argument("order response must be an object");

        const auto &success = required_field(parsed, "success");
        if (!success.is_boolean())
            throw std::invalid_argument("response field success must be a boolean");

        OrderResponse response;
        response.success = success.get<bool>();
        response.order_id = required_string(parsed, "orderID");
        response.status = required_string(parsed, "status");
        optional_string(parsed, "errorMsg", response.error_msg);
        optional_string(parsed, "takingAmount", response.taking_amount);
        optional_string(parsed, "makingAmount", response.making_amount);

        if (response.success && (response.order_id.empty() || response.status.empty()))
            throw std::invalid_argument(
                "successful order response requires non-empty orderID and status");

        optional_string_array(parsed, "transactionsHashes", "transactionHashes",
                              response.transaction_hashes);
        optional_string_array(parsed, "tradeIDs", "tradeIds", response.trade_ids);
        return response;
    }

    ParsedCancellationResponse parse_cancellation_response_json(
        const std::string &json_text)
    {
        const auto parsed = json::parse(json_text);
        if (!parsed.is_object())
            throw std::invalid_argument("cancellation response must be an object");

        const auto *canceled = optional_defaulted_field(parsed, "canceled");
        const auto *not_canceled = optional_defaulted_field(
            parsed, "notCanceled", "not_canceled");
        if (canceled && !canceled->is_array())
            throw std::invalid_argument("response field canceled must be an array");
        if (not_canceled && !not_canceled->is_object())
            throw std::invalid_argument("response field notCanceled must be an object");

        ParsedCancellationResponse response;
        if (canceled)
        {
            for (const auto &id : *canceled)
            {
                if (!id.is_string())
                    throw std::invalid_argument("canceled order IDs must be strings");
                response.canceled.insert(id.get<std::string>());
            }
        }
        if (not_canceled)
        {
            for (auto it = not_canceled->begin(); it != not_canceled->end(); ++it)
            {
                if (!it.value().is_string())
                    throw std::invalid_argument(
                        "cancellation failure reasons must be strings");
                response.not_canceled.emplace(it.key(), it.value().get<std::string>());
            }
        }
        return response;
    }
}
