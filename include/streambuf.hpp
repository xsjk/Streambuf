#pragma once

#include <mutex>
#include <boost/asio.hpp>
#include <ranges>
#include <list>

using namespace std::chrono_literals;

#define def constexpr auto 

/**
 * @brief A interface that adds some functionalities to a view.
 * @note `cbegin()`, `cend()` `rbegin()`, `rend()`, `crbegin()`, `crend()`
 *       `size()`, `empty()`, `front()`, `back()`, `operator[]`, `operator bool()`
 * will be defined if possible.
 * @note You have to define `begin()` and `end()` in the derived class.
 * @tparam D the derived class
 */
template<class D>
    requires std::is_class_v<D> &&std::same_as<D, std::remove_cv_t<D>>
class view_interface : public std::ranges::view_interface<D> {
    def &derived(this auto &&self) noexcept { return static_cast<std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const D &, D &>>(self); }
public:
    def rbegin(this auto &&self) noexcept requires std::ranges::bidirectional_range<D> { return std::make_reverse_iterator(self.derived().end()); }
    def rend(this auto &&self) noexcept requires std::ranges::bidirectional_range<D> { return std::make_reverse_iterator(self.derived().begin()); }
    def crbegin(this auto &&self) noexcept requires std::ranges::bidirectional_range<D> { return std::make_reverse_iterator(self.derived().cbegin()); }
    def crend(this auto &&self) noexcept requires std::ranges::bidirectional_range<D> { return std::make_reverse_iterator(self.derived().cend()); }
};


template<typename T, size_t N = 0, class S = std::array<T, N>>
class StreamBuffer : public view_interface<StreamBuffer<T, N, S>> {

    static_assert(N > 0, "StreamBuffer size must be greater than 0");
    static_assert(std::is_same_v<std::remove_const_t<T>, T>, "StreamBuffer must have a non-const value type");
    static_assert(std::ranges::contiguous_range<S>, "StreamBuffer storage must be a contiguous range");

    static def get_distance(size_t start, size_t end) noexcept { return end >= start ? end - start : N - (start - end); }

    template<class V>
        requires std::is_same_v<std::remove_const_t<V>, T>
    struct normal_iterator {
        using value_type = T;
        V *storage;     // V = T* or const T*
        size_t start;   // start of the storage
        size_t offset;  // offset of the storage
        def &operator*() const noexcept { return storage[offset]; }
        def *operator->() const noexcept { return &storage[offset]; }
        def &operator++() noexcept { ++offset; if (offset == N) offset = 0; return *this; }
        def &operator--() noexcept { offset = offset == 0 ? N - 1 : offset - 1; return *this; }
        def operator++(int) noexcept { auto temp = *this; ++*this; return temp; }
        def operator--(int) noexcept { auto temp = *this; --*this; return temp; }
        def &operator+=(size_t n) noexcept { offset = (offset + n) % N; return *this; }
        def &operator-=(size_t n) noexcept { n %= N; offset = offset >= n ? offset - n : N - (n - offset); return *this; }
        def operator+(size_t n) const noexcept { auto temp = *this; return temp += n; }
        def friend operator+(size_t n, const normal_iterator<V> &it) noexcept { return it + n; }
        def operator-(size_t n) const noexcept { auto temp = *this; return temp -= n; }
        def &operator[](size_t n) const noexcept { return *(*this + n); }
        def operator-(const normal_iterator<V> &other) const noexcept { return (long long)(index()) - (long long)(other.index()); }
        def operator==(const normal_iterator<V> &other) const noexcept { return offset == other.offset; }
        def operator<=> (const normal_iterator<V> &other) const noexcept { return index() <=> other.index(); }
        def index() const noexcept { return get_distance(start, offset); }
    };

    def make_iterator(this auto &&self, size_t offset) noexcept { return normal_iterator { self.storage.data(), self.start, offset }; }

    S storage;
    size_t before_start = 0;   // the start of the read memory, the end of unuse memory
    size_t start = 0;          // the start of the owned memory, the end of the read memory
    size_t stop = 0;           // the end of the owned memory, the start of the prepared memory
    size_t after_stop = 0;     // the end of the prepared memory, the start of the unuse memory

    template<size_t R>
    struct BorrowManager {
        StreamBuffer<T, N, S> &buffer;
        size_t &before;
        size_t &after;
        size_t &max_after;
        std::list<size_t> nodes {};
        std::mutex mutex {};

        struct owning_view : view_interface<owning_view> {
            using value_type = T;
            def begin() const noexcept -> normal_iterator<T>;
            def end() const noexcept -> normal_iterator<T>;
            owning_view(const owning_view &) = delete;
            owning_view(owning_view &&other) { swap(other); }
            owning_view &operator=(const owning_view &) = delete;
            owning_view &operator=(owning_view &&other) { swap(other); return *this; }
            owning_view(BorrowManager *manager, size_t n) : manager { manager } {
                std::lock_guard lock(manager->mutex);
                size_t before = manager->before;
                size_t after = manager->after;
                size_t max_after = manager->max_after;
                size_t available_size = get_distance(after + R, max_after);
                if (n > available_size)
                    throw std::out_of_range("borrow size too large");
                manager->nodes.push_back(after);
                it = std::prev(manager->nodes.end());
                start = after;
                stop = (after + n) % N;
                manager->after = stop;
            }
            ~owning_view() {
                if (manager == nullptr) return;
                std::lock_guard lock(manager->mutex);
                it = manager->nodes.erase(it);
                if (it == manager->nodes.begin()) {
                    size_t after = manager->after;
                    if (it == manager->nodes.end())
                        manager->before = after;
                    else
                        manager->before = *it;
                }
            }
        private:
            void swap(this auto &&self, owning_view &other) noexcept {
                std::swap(self.manager, other.manager);
                std::swap(self.start, other.start);
                std::swap(self.stop, other.stop);
                std::swap(self.it, other.it);
            }
            BorrowManager *manager = nullptr;
            size_t start;
            size_t stop;
            std::list<size_t>::iterator it;
        };

        def lend(size_t n) { return owning_view(this, n); }
    };
    BorrowManager<0> read_manager { *this, before_start, start, stop };
    BorrowManager<1> write_manager { *this, stop, after_stop, before_start };

public:

    using value_type = T;

    using iterator = normal_iterator<T>;
    using const_iterator = normal_iterator<const T>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using read_view = typename decltype(read_manager)::owning_view;
    using write_view = typename decltype(write_manager)::owning_view;

    template<typename ...Args> requires requires { S { std::declval<Args>()... }; }
    StreamBuffer(Args &&...args) noexcept : storage { std::forward<Args>(args)... } { }
    StreamBuffer(const StreamBuffer<T, N, S> &other) noexcept : storage { other.storage }, start { other.start.load() }, stop { other.stop.load() } { }
    StreamBuffer(StreamBuffer<T, N, S> &&other) noexcept { swap(other); }
    def &operator=(const StreamBuffer<T, N, S> &other) noexcept { StreamBuffer<T, N, S>(other).swap(*this); return *this; }
    def &operator=(StreamBuffer<T, N, S> &&other) noexcept { swap(other); return *this; }

    def begin(this auto &&self) noexcept { return self.make_iterator(self.start); }
    def end(this auto &&self) noexcept { return self.make_iterator(self.stop); }
    def clear() noexcept {
        std::lock_guard read_lock(read_manager.mutex);
        std::lock_guard write_lock(write_manager.mutex);
        before_start = start = stop = after_stop = 0;
    }
    def size() const noexcept { return get_distance(start, stop); }
    def max_size() const noexcept { return N - 1; }
    def full() const noexcept { return (stop + 1) % N == start; }
    def empty() const noexcept { return start == stop; }
    def &at(this auto &&self, size_t index) { self.check_index(index); return self[index]; }

    def swap(StreamBuffer<T, N> &other) noexcept {
        std::lock_guard read_lock(read_manager.mutex);
        std::lock_guard write_lock(write_manager.mutex);
        std::lock_guard other_read_lock(other.read_manager.mutex);
        std::lock_guard other_write_lock(other.write_manager.mutex);
        std::swap(storage, other.storage);
        std::swap(before_start, other.before_start);
        std::swap(start, other.start);
        std::swap(stop, other.stop);
        std::swap(after_stop, other.after_stop);
    }

    def prepare(size_t n) -> write_view { return write_manager.lend(n); }
    def read(size_t n) -> read_view { return read_manager.lend(n); }
    def read() -> read_view { return read_manager.lend(size()); }

    boost::asio::awaitable<write_view> async_prepare(size_t n) {
        while (true) {
            try { co_return prepare(n); }
            catch (std::out_of_range &) { }
            co_await async_sleep(0ms);
        }
    }

    boost::asio::awaitable<read_view> async_read(auto &&...args) {
        while (true) {
            try { co_return read(std::forward<decltype(args)>(args)...); }
            catch (std::out_of_range &) { }
            co_await async_sleep(0ms);
        }
    }

    operator std::string(this auto &&self) noexcept { return std::format("StreamBuffer {{ start = {}, stop = {}, size = {} }}", self.start, self.stop, self.size()); }
    friend auto &operator<<(auto &os, const StreamBuffer<T, N> &buf) { return os << std::string(buf); }

private:

    static boost::asio::awaitable<void> async_sleep(auto time) {
        co_await boost::asio::steady_timer(co_await boost::asio::this_coro::executor, time).async_wait(boost::asio::use_awaitable);
    }

    void check_index(this auto &&self, size_t index) { if (index >= self.size()) throw std::out_of_range("index out of range"); }

};

template<typename T, size_t N, class S>
template<size_t R>
def StreamBuffer<T, N, S>::BorrowManager<R>::owning_view::begin() const noexcept -> StreamBuffer<T, N, S>::iterator {
    return manager->buffer.make_iterator(start);
}

template<typename T, size_t N, class S>
template<size_t R>
def StreamBuffer<T, N, S>::BorrowManager<R>::owning_view::end() const noexcept -> StreamBuffer<T, N, S>::iterator {
    return manager->buffer.make_iterator(stop);
}

#undef def

static_assert(std::random_access_iterator<StreamBuffer<int, 1>::iterator>, "StreamBuffer::iterator must be a random access iterator");
static_assert(std::random_access_iterator<StreamBuffer<int, 1>::const_iterator>, "StreamBuffer::const_iterator must be a random access iterator");
static_assert(std::random_access_iterator<StreamBuffer<int, 1>::reverse_iterator>, "StreamBuffer::reverse_iterator must be a random access iterator");
static_assert(std::random_access_iterator<StreamBuffer<int, 1>::const_reverse_iterator>, "StreamBuffer::const_reverse_iterator must be a random access iterator");
static_assert(std::ranges::random_access_range<StreamBuffer<int, 1>>, "StreamBuffer must be a random access range");
static_assert(std::ranges::sized_range<StreamBuffer<int, 1>>, "StreamBuffer must be a sized range");
static_assert(std::ranges::constant_range<const StreamBuffer<int, 1>>, "const StreamBuffer must be a constant range");
static_assert(!std::ranges::constant_range<StreamBuffer<int, 1>>, "StreamBuffer must not be a constant range");

