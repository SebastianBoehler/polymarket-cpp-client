#pragma once

namespace polymarket::detail
{
    inline thread_local const void *active_internal_callback = nullptr;

    class InternalCallbackScope
    {
    public:
        explicit InternalCallbackScope(const void *owner)
            : previous_(active_internal_callback)
        {
            active_internal_callback = owner;
        }

        ~InternalCallbackScope() { active_internal_callback = previous_; }

    private:
        const void *previous_;
    };

    inline bool is_internal_callback(const void *owner)
    {
        return active_internal_callback == owner;
    }
}
