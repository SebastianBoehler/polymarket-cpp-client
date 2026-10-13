#pragma once

#include "polymarket/sdk_error.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace polymarket
{
    namespace detail
    {
        class OpaqueCursorPaginationError : public std::runtime_error
        {
          public:
            using std::runtime_error::runtime_error;
        };

        class OpaqueCursorPagination
        {
          public:
            static constexpr size_t max_pages = 1000;

            OpaqueCursorPagination(std::string initial_cursor, std::string end_cursor)
                : cursor_(std::move(initial_cursor)), end_cursor_(std::move(end_cursor))
            {
            }

            const std::string &cursor() const { return cursor_; }

            bool begin_page()
            {
                if (cursor_.empty() || cursor_ == end_cursor_) return false;
                if (pages_started_ >= max_pages)
                    throw OpaqueCursorPaginationError(
                        "opaque cursor pagination exceeded 1000 pages");
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
    } // namespace detail

    // The next page to request. Cursor pagination fills `cursor`; offset
    // pagination fills `offset` and `limit`. Pass it back to resume later.
    struct PageRequest
    {
        std::string cursor;
        size_t offset{0};
        size_t limit{0};
    };

    // One fetched page. `next_cursor` is read only by cursor pagination.
    template <typename T> struct Page
    {
        std::vector<T> items;
        std::string next_cursor;
    };

    // Pulls pages lazily through `fetch`. Cursor pagination ends at an empty or
    // end cursor and fails after max_pages or on a cycle; the page whose next
    // cursor repeats is still returned and the call after it fails. Offset pagination ends
    // on a short page and stops with truncated() set once the next offset would
    // pass `max_offset`; items inserted between requests can shift across pages.
    // After a failure every call reports the same error. Not thread-safe; any
    // client captured by `fetch` must outlive the paginator.
    template <typename T> class Paginator
    {
      public:
        using Fetch = std::function<Result<Page<T>>(const PageRequest &)>;
        class ItemRange;

        static Paginator cursor(std::string endpoint, Fetch fetch, std::string initial_cursor,
                                std::string end_cursor)
        {
            Paginator paginator(std::move(endpoint), std::move(fetch));
            paginator.cursor_.emplace(std::move(initial_cursor), std::move(end_cursor));
            return paginator;
        }

        static Paginator offset(std::string endpoint, Fetch fetch, size_t limit, size_t max_offset,
                                size_t start_offset = 0)
        {
            if (limit == 0) throw std::invalid_argument("pagination limit must be positive");
            Paginator paginator(std::move(endpoint), std::move(fetch));
            paginator.limit_ = limit;
            paginator.max_offset_ = max_offset;
            paginator.offset_ = start_offset;
            paginator.truncated_ = start_offset > max_offset;
            paginator.done_ = paginator.truncated_;
            return paginator;
        }

        // nullopt once exhausted. A page may be empty.
        Result<std::optional<std::vector<T>>> next_page()
        {
            using PageResult = Result<std::optional<std::vector<T>>>;
            if (error_) return PageResult::failure(*error_);
            if (done_) return PageResult::success(std::nullopt);
            return cursor_ ? next_cursor_page() : next_offset_page();
        }

        // Drains remaining pages, keeping at most `max_items`. resume_token()
        // then points past the last fetched page, not past the last kept item.
        Result<std::vector<T>> collect(size_t max_items = std::numeric_limits<size_t>::max())
        {
            std::vector<T> items;
            while (items.size() < max_items)
            {
                auto page = next_page();
                if (!page.ok()) return Result<std::vector<T>>::failure(page.error());
                if (!page.value()) break;
                auto &batch = *page.value();
                const size_t keep = std::min(batch.size(), max_items - items.size());
                items.insert(
                    items.end(), std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.begin() + static_cast<std::ptrdiff_t>(keep)));
            }
            return Result<std::vector<T>>::success(std::move(items));
        }

        // Range-for over remaining items. Iteration stops on failure; check
        // error() afterwards. After a break, items() resumes at the item last
        // returned. next_page() and collect() skip items still buffered here.
        // The paginator must stay in place while iterating.
        ItemRange items() { return ItemRange(*this); }

        const std::optional<SdkError> &error() const { return error_; }
        bool truncated() const { return truncated_; }
        bool done() const { return done_ || error_.has_value(); }

        PageRequest resume_token() const
        {
            if (cursor_) return PageRequest{cursor_->cursor(), 0, 0};
            return PageRequest{"", offset_, limit_};
        }

        class ItemIterator
        {
          public:
            using value_type = T;
            using difference_type = std::ptrdiff_t;

            ItemIterator() = default;
            explicit ItemIterator(Paginator &paginator) : paginator_(&paginator)
            {
                if (paginator_->buffer_index_ >= paginator_->buffer_.size())
                    paginator_->fill_buffer();
            }

            T &operator*() const { return paginator_->buffer_[paginator_->buffer_index_]; }
            T *operator->() const { return &**this; }

            ItemIterator &operator++()
            {
                if (++paginator_->buffer_index_ >= paginator_->buffer_.size())
                    paginator_->fill_buffer();
                return *this;
            }
            void operator++(int) { ++*this; }

            friend bool operator==(const ItemIterator &it, std::default_sentinel_t)
            {
                return it.at_end();
            }

          private:
            bool at_end() const
            {
                return paginator_ == nullptr ||
                       paginator_->buffer_index_ >= paginator_->buffer_.size();
            }

            Paginator *paginator_{nullptr};
        };

        class ItemRange
        {
          public:
            explicit ItemRange(Paginator &paginator) : paginator_(&paginator) {}
            ItemIterator begin() const { return ItemIterator(*paginator_); }
            std::default_sentinel_t end() const { return {}; }

          private:
            Paginator *paginator_;
        };

      private:
        Paginator(std::string endpoint, Fetch fetch)
            : endpoint_(std::move(endpoint)), fetch_(std::move(fetch))
        {
            if (!fetch_) throw std::invalid_argument("pagination fetch must be set");
        }

        Result<std::optional<std::vector<T>>> fail(SdkError error)
        {
            error_ = std::move(error);
            return Result<std::optional<std::vector<T>>>::failure(*error_);
        }

        Result<std::optional<std::vector<T>>> next_cursor_page()
        {
            using PageResult = Result<std::optional<std::vector<T>>>;
            try
            {
                if (!cursor_->begin_page())
                {
                    done_ = true;
                    return PageResult::success(std::nullopt);
                }
            }
            catch (const detail::OpaqueCursorPaginationError &ex)
            {
                return fail(make_parse_error(ex.what(), endpoint_));
            }

            auto page = fetch_(PageRequest{cursor_->cursor(), 0, 0});
            if (!page.ok()) return fail(page.error());
            try
            {
                cursor_->advance(std::move(page.value().next_cursor));
            }
            catch (const detail::OpaqueCursorPaginationError &ex)
            {
                error_ = make_parse_error(ex.what(), endpoint_);
            }
            return PageResult::success(std::move(page.value().items));
        }

        Result<std::optional<std::vector<T>>> next_offset_page()
        {
            using PageResult = Result<std::optional<std::vector<T>>>;
            auto page = fetch_(PageRequest{"", offset_, limit_});
            if (!page.ok()) return fail(page.error());
            auto &items = page.value().items;
            if (items.size() > limit_)
                return fail(make_parse_error("page holds " + std::to_string(items.size()) +
                                                 " items, more than limit " +
                                                 std::to_string(limit_),
                                             endpoint_));
            if (items.size() < limit_)
                done_ = true;
            else if (max_offset_ - offset_ < limit_)
                done_ = truncated_ = true;
            else
                offset_ += limit_;
            return PageResult::success(std::move(items));
        }

        void fill_buffer()
        {
            buffer_.clear();
            buffer_index_ = 0;
            while (true)
            {
                auto page = next_page();
                if (!page.ok() || !page.value()) return;
                if (!page.value()->empty())
                {
                    buffer_ = std::move(*page.value());
                    return;
                }
            }
        }

        std::string endpoint_;
        Fetch fetch_;
        std::optional<detail::OpaqueCursorPagination> cursor_;
        size_t offset_{0};
        size_t limit_{0};
        size_t max_offset_{0};
        bool done_{false};
        bool truncated_{false};
        std::optional<SdkError> error_;
        std::vector<T> buffer_;
        size_t buffer_index_{0};
    };
} // namespace polymarket
