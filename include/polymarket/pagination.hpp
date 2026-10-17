#pragma once

#include "polymarket/sdk_error.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
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

            OpaqueCursorPagination(std::string initial_cursor, std::string end_cursor,
                                   bool first_page_without_cursor = false)
                : cursor_(std::move(initial_cursor)), end_cursor_(std::move(end_cursor)),
                  first_page_without_cursor_(first_page_without_cursor && cursor_.empty())
            {
            }

            const std::string &cursor() const { return cursor_; }

            bool exhausted() const
            {
                return !first_page_without_cursor_ && (cursor_.empty() || cursor_ == end_cursor_);
            }

            bool begin_page()
            {
                const bool first_page = std::exchange(first_page_without_cursor_, false);
                if (!first_page && (cursor_.empty() || cursor_ == end_cursor_)) return false;
                if (pages_started_ >= max_pages)
                    throw OpaqueCursorPaginationError(
                        "opaque cursor pagination exceeded 1000 pages");
                if (!first_page && !seen_cursors_.insert(cursor_).second)
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
            bool first_page_without_cursor_{false};
            size_t pages_started_{0};
            std::unordered_set<std::string> seen_cursors_;
        };
    } // namespace detail

    // The next page to request. Cursor and keyset pagination fill `cursor`
    // (empty for a keyset first page), offset pagination fills `offset`, and
    // numbered pagination fills `page` (from 1); all but cursor fill `limit`.
    // Pass it back to resume later. An exhausted cursor holds the end cursor,
    // an offset past the cap resumes as truncated without fetching, and
    // keyset and numbered pagination set `exhausted` after their last page.
    struct PageRequest
    {
        std::string cursor;
        size_t offset{0};
        size_t limit{0};
        size_t page{0};
        bool exhausted{false};
    };

    // One fetched page. `next_cursor` is read by cursor and keyset pagination,
    // `counted` by offset pagination when the limit applies to fewer rows than
    // `items` holds (top-level comments with their replies), and `has_more` by
    // numbered pagination.
    template <typename T> struct Page
    {
        std::vector<T> items;
        std::string next_cursor;
        std::optional<size_t> counted;
        bool has_more{false};
    };

    // Pulls pages lazily through `fetch`. Cursor pagination ends at an empty or
    // end cursor and fails after max_pages or on a cycle; the page whose next
    // cursor repeats is still returned and the call after it fails. Keyset
    // pagination is cursor pagination whose first page has no cursor and whose
    // last page has an empty next cursor. Offset pagination ends on a short
    // page and stops with truncated() set once the next offset would pass
    // `max_offset`; items inserted between requests can shift across pages.
    // Numbered pagination ends when a page reports no more results and fails
    // after max_pages.
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
            Paginator paginator(std::move(endpoint), std::move(fetch), Strategy::cursor, 0);
            paginator.cursor_.emplace(std::move(initial_cursor), std::move(end_cursor));
            return paginator;
        }

        static Paginator keyset(std::string endpoint, Fetch fetch, size_t limit,
                                const PageRequest &resume = {})
        {
            Paginator paginator(std::move(endpoint), std::move(fetch), Strategy::keyset, limit);
            paginator.cursor_.emplace(resume.cursor, "", true);
            paginator.done_ = resume.exhausted;
            return paginator;
        }

        static Paginator numbered(std::string endpoint, Fetch fetch, size_t limit,
                                  const PageRequest &resume = {})
        {
            Paginator paginator(std::move(endpoint), std::move(fetch), Strategy::numbered, limit);
            paginator.page_ = resume.page == 0 ? 1 : resume.page;
            paginator.done_ = resume.exhausted;
            return paginator;
        }

        static Paginator offset(std::string endpoint, Fetch fetch, size_t limit, size_t max_offset,
                                size_t start_offset = 0)
        {
            Paginator paginator(std::move(endpoint), std::move(fetch), Strategy::offset, limit);
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
            switch (strategy_)
            {
            case Strategy::offset:
                return next_offset_page();
            case Strategy::numbered:
                return next_numbered_page();
            case Strategy::cursor:
            case Strategy::keyset:
                break;
            }
            return next_cursor_page();
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
            const bool exhausted = done_ && !error_;
            switch (strategy_)
            {
            case Strategy::cursor:
                return PageRequest{cursor_->cursor(), 0, 0};
            case Strategy::keyset:
                return PageRequest{cursor_->cursor(), 0, limit_, 0, exhausted};
            case Strategy::numbered:
                return PageRequest{"", 0, limit_, page_, exhausted};
            case Strategy::offset:
                break;
            }
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
        enum class Strategy : std::uint8_t
        {
            cursor,
            keyset,
            offset,
            numbered
        };

        Paginator(std::string endpoint, Fetch fetch, Strategy strategy, size_t limit)
            : endpoint_(std::move(endpoint)), fetch_(std::move(fetch)), strategy_(strategy),
              limit_(limit)
        {
            if (!fetch_) throw std::invalid_argument("pagination fetch must be set");
            if (strategy_ != Strategy::cursor && limit_ == 0)
                throw std::invalid_argument("pagination limit must be positive");
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

            auto page = fetch_(PageRequest{cursor_->cursor(), 0, limit_});
            if (!page.ok()) return fail(page.error());
            try
            {
                cursor_->advance(std::move(page.value().next_cursor));
                done_ = cursor_->exhausted();
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
            const size_t counted = page.value().counted.value_or(items.size());
            if (counted > limit_ || counted > items.size())
                return fail(make_parse_error("page counts " + std::to_string(counted) + " of " +
                                                 std::to_string(items.size()) +
                                                 " items against limit " + std::to_string(limit_),
                                             endpoint_));
            const bool short_page = counted < limit_;
            const bool at_offset_cap = !short_page && max_offset_ - offset_ < limit_;
            offset_ = counted > std::numeric_limits<size_t>::max() - offset_
                          ? std::numeric_limits<size_t>::max()
                          : offset_ + counted;
            if (short_page)
                done_ = true;
            else if (at_offset_cap)
                done_ = truncated_ = true;
            return PageResult::success(std::move(items));
        }

        Result<std::optional<std::vector<T>>> next_numbered_page()
        {
            using PageResult = Result<std::optional<std::vector<T>>>;
            if (pages_started_ >= detail::OpaqueCursorPagination::max_pages)
                return fail(make_parse_error("numbered pagination exceeded 1000 pages", endpoint_));
            if (page_ == std::numeric_limits<size_t>::max())
                return fail(make_parse_error("numbered pagination page overflow", endpoint_));
            ++pages_started_;
            auto page = fetch_(PageRequest{"", 0, limit_, page_});
            if (!page.ok()) return fail(page.error());
            ++page_;
            done_ = !page.value().has_more;
            return PageResult::success(std::move(page.value().items));
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
        Strategy strategy_;
        std::optional<detail::OpaqueCursorPagination> cursor_;
        size_t offset_{0};
        size_t limit_{0};
        size_t page_{0};
        size_t pages_started_{0};
        size_t max_offset_{0};
        bool done_{false};
        bool truncated_{false};
        std::optional<SdkError> error_;
        std::vector<T> buffer_;
        size_t buffer_index_{0};
    };
} // namespace polymarket
