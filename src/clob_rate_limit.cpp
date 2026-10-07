#include "polymarket/clob_client.hpp"
#include "rate_limit_internal.hpp"

#include <utility>

namespace polymarket
{
    void ClobClient::set_rate_limit_listener(std::function<void(const RateLimitUpdate &)> listener)
    {
        auto shared =
            listener ? std::make_shared<const RateLimitListener>(std::move(listener)) : nullptr;
        std::lock_guard<std::mutex> lock(rate_limit_mutex_);
        rate_limit_listener_ = std::move(shared);
    }

    void ClobClient::notify_rate_limit(const HttpResponse &response,
                                       RateLimitUpdate::Bucket bucket) const
    {
        std::shared_ptr<const RateLimitListener> listener;
        {
            std::lock_guard<std::mutex> lock(rate_limit_mutex_);
            listener = rate_limit_listener_;
        }
        if (!listener) return;
        const auto update = detail::parse_rate_limit_update(response, bucket);
        if (!update) return;
        try
        {
            (*listener)(*update);
        }
        catch (...)
        {
            // A failing listener must not change the outcome of the request.
        }
    }

} // namespace polymarket
