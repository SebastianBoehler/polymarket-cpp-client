#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace polymarket::detail
{
    class OpaqueCursorPaginationError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    class OpaqueCursorPagination
    {
    public:
        static constexpr size_t MAX_PAGES = 1000;

        OpaqueCursorPagination(std::string initial_cursor, std::string end_cursor)
            : cursor_(std::move(initial_cursor)), end_cursor_(std::move(end_cursor))
        {
        }

        const std::string &cursor() const { return cursor_; }

        bool begin_page()
        {
            if (cursor_.empty() || cursor_ == end_cursor_)
                return false;
            if (pages_started_ >= MAX_PAGES)
                throw OpaqueCursorPaginationError("opaque cursor pagination exceeded 1000 pages");
            if (!seen_cursors_.insert(cursor_).second)
                throw OpaqueCursorPaginationError("opaque cursor pagination cycle detected");
            ++pages_started_;
            return true;
        }

        void advance(std::string next_cursor)
        {
            if (next_cursor.empty() || next_cursor == end_cursor_)
            {
                cursor_ = end_cursor_;
                return;
            }
            if (seen_cursors_.contains(next_cursor))
                throw OpaqueCursorPaginationError("opaque cursor pagination cycle detected");
            cursor_ = std::move(next_cursor);
        }

    private:
        std::string cursor_;
        std::string end_cursor_;
        size_t pages_started_{0};
        std::unordered_set<std::string> seen_cursors_;
    };
}
