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

    /**
     * @brief A manager to manage the collection of lent views.
     * @note The manager can lend views for reading or writing efficiently,
     *       and will keep track of the lent views and the available space or data.
     * @tparam R the number of elements to reserve. Due to the circular nature of the buffer, 
     *         One element should be reserved when two indices go across the end of the buffer.
     */
    template<size_t R>
    struct Manager {
        StreamBuffer<T, N, S> &buffer;
        size_t &lent_begin;         // The beginning of the oldest lent view, may be increased when returning.
        size_t &lendable_begin;     // The beginning of the lendable space, may be increased when lending.
        const size_t &lendable_end; // The end of lendable space, read only.
        std::list<size_t> nodes {}; // the nodes of the lent views
        std::mutex mutex {};        // the mutex to protect the nodes

        /**
         * @brief A view that owns a part of the buffer.
         * @note The view will automatically return its memory to the manager at destruction.
         */
        struct owning_view : view_interface<owning_view> {
            def begin() const noexcept -> normal_iterator<T> {
                return {manager->buffer.storage.data(), start, start};
            }
            def end() const noexcept -> normal_iterator<T> {
                return {manager->buffer.storage.data(), start, stop};
            }
            owning_view() = delete;
            owning_view(const owning_view &) = delete;
            owning_view(owning_view &&other) {
                std::lock_guard lock(other.manager->mutex);
                swap(other);
            }
            owning_view &operator=(const owning_view &) = delete;
            owning_view &operator=(owning_view &&other) {
                std::scoped_lock lock(manager->mutex, other.manager->mutex);
                swap(other);
                return *this;
            }
            ~owning_view() {
                if (manager == nullptr) return;
                std::lock_guard lock(manager->mutex);
                it = manager->nodes.erase(it);
                if (it == manager->nodes.begin())
                    manager->lent_begin = (it == manager->nodes.end()) ? manager->lendable_begin : *it;
            }
        private:
            friend class Manager;
            owning_view(Manager *manager, size_t n) : manager { manager } {
                std::lock_guard lock(manager->mutex);
                size_t lendable_begin = manager->lendable_begin;
                size_t available_size = get_distance(lendable_begin + R, manager->lendable_end);
                if (n > available_size)
                    throw std::out_of_range("borrow size too large");
                manager->nodes.push_back(lendable_begin);
                it = std::prev(manager->nodes.end());
                start = lendable_begin;
                stop = (lendable_begin + n) % N;
                manager->lendable_begin = stop;
            }
            owning_view(Manager *manager) : manager { manager } {
                std::lock_guard lock(manager->mutex);
                start = manager->lendable_begin;
                size_t n = get_distance(start + R, manager->lendable_end);
                manager->nodes.push_back(start);
                it = std::prev(manager->nodes.end());
                stop = (start + n) % N;
                manager->lendable_begin = stop;
            }
            void swap(this auto &&self, owning_view &other) noexcept {
                std::swap(self.manager, other.manager);
                std::swap(self.start, other.start);
                std::swap(self.stop, other.stop);
                std::swap(self.it, other.it);
            }
            Manager *manager = nullptr;
            size_t start;
            size_t stop;
            std::list<size_t>::iterator it; // the iterator of `start` in `manager->nodes`
        };

        /**
         * @brief Lend a view from the manager.
         * @param n the size to lend
         * @return a view for reading or writing
         * @throw std::out_of_range if not enough space or data is available
         */
        def lend(size_t n) { return owning_view(this, n); }

        /**
         * @brief Lend all available space from the manager.
         * @return a view for reading or writing
         * @note This function will not throw and will return an empty view if no space or data is available.
         */
        def lend() noexcept { return owning_view(this); }
    };

    Manager<0> read_manager { *this, before_start, start, stop };       // The manager for `read()`.
    Manager<1> write_manager { *this, stop, after_stop, before_start }; // The manager for `prepare()`.

    /**************************************** UTILITIES ****************************************/

    /**
     * @brief Asyncronously sleep for a while
     * @param time
     */
    static boost::asio::awaitable<void> async_sleep(auto time) noexcept {
        co_await boost::asio::steady_timer(co_await boost::asio::this_coro::executor, time).async_wait(boost::asio::use_awaitable);
    }

    /**
     * @brief Check if the index is out of range
     * @param index
     * @throw std::out_of_range if the index is out of range
     */
    void check_index(this auto &&self, size_t index) {
        if (index >= self.size())
            throw std::out_of_range("index out of range");
    }

    def swap(StreamBuffer<T, N> &other) noexcept {
        std::swap(storage, other.storage);
        std::swap(before_start, other.before_start);
        std::swap(start, other.start);
        std::swap(stop, other.stop);
        std::swap(after_stop, other.after_stop);
    }

    void assign(const StreamBuffer<T, N, S> &other) noexcept {
        storage = other.storage;
        before_start = other.before_start;
        start = other.start;
        stop = other.stop;
        after_stop = other.after_stop;
    }

public:

    using iterator = normal_iterator<T>;
    using const_iterator = normal_iterator<const T>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using read_view = typename decltype(read_manager)::owning_view;
    using write_view = typename decltype(write_manager)::owning_view;

    /************************** CONSTRUCTORS **************************/
    
    template<typename ...Args> requires requires { S { std::declval<Args>()... }; }
    StreamBuffer(Args &&...args) noexcept : storage { std::forward<Args>(args)... } { }
    StreamBuffer(StreamBuffer<T, N, S> &other) noexcept {
        std::scoped_lock lock(other.read_manager.mutex, other.write_manager.mutex);
        assign(other);
    }
    StreamBuffer(const StreamBuffer<T, N, S> &other) = default;
    StreamBuffer(StreamBuffer<T, N, S> &&other) noexcept {
        std::scoped_lock lock(other.read_manager.mutex, other.write_manager.mutex);
        swap(other);
    }
    def &operator=(StreamBuffer<T, N, S> &other) noexcept {
        std::scoped_lock lock(read_manager.mutex, write_manager.mutex, other.read_manager.mutex, other.write_manager.mutex);
        assign(other);
        return *this;
    }
    def &operator=(const StreamBuffer<T, N, S> &other) noexcept {
        std::scoped_lock lock(read_manager.mutex, write_manager.mutex);
        assign(other);
        return *this;
    }
    def &operator=(StreamBuffer<T, N, S> &&other) noexcept {
        std::scoped_lock lock(read_manager.mutex, write_manager.mutex, other.read_manager.mutex, other.write_manager.mutex);
        swap(other);
        return *this;
    }

    /******************************** ITERATOR ******************************/
    // const iterators, reverse iterators and const reverse iterators are defined by view_interface

    def begin(this auto &&self) noexcept { return self.make_iterator(self.start); }
    def end(this auto &&self) noexcept { return self.make_iterator(self.stop); }


    /******************************** ACCESS ********************************/

    /**
     * @brief Clear the buffer
     */
    def clear() noexcept {
        std::scoped_lock lock(read_manager.mutex, write_manager.mutex);
        before_start = start = stop = after_stop = 0;
    }
    /**
     * @brief Get the size of the buffer
     * @return the size of the buffer as `size_t`
     */
    def size() const noexcept { return get_distance(start, stop); }

    /**
     * @brief Get the maximum size of the buffer
     * @return the maximum size of the buffer as `size_t`
     */
    def max_size() const noexcept { return N - 1; }

    /**
     * @brief Check if the buffer is full
     * @return `true` if the buffer is full, `false` otherwise
     */
    def full() const noexcept { return (stop + 1) % N == start; }

    /**
     * @brief Check if the buffer is empty
     * @return `true` if the buffer is empty, `false` otherwise
     */
    def empty() const noexcept { return start == stop; }

    /**
     * @brief Get the first element
     * @return the first element as `T &`
     * @note This function will not check if the buffer is empty.
     */
    def &front(this auto &&self) noexcept { return self.storage[self.start]; }

    /**
     * @brief Get the last element
     * @return the last element as `T &`
     * @note This function will not check if the buffer is empty.
     */
    def &back(this auto &&self) noexcept { return self.storage[(self.stop + N - 1) % N]; }

    /**
     * @brief Get the element at the given index
     * @param index the index of the element
     * @return the element as `T &`
     * @throw std::out_of_range if the index is out of range
     */
    def &at(this auto &&self, size_t index) { self.check_index(index); return self[index]; }

    /**
     * @brief Get the element at the given index
     * @param index the index of the element
     * @return the element as `T &`
     * @note This function will not check if the index is out of range.
     */
    def &operator[](this auto &&self, size_t index) noexcept { return self.storage[(self.start + index) % N]; }


    /******************************** IO ********************************/

    /**
     * @brief Prepare a space for writing
     * @param n the size to prepare
     * @return a view for writing
     * @throw std::out_of_range if not enough space is available
     */
    def prepare(size_t n) -> write_view { return write_manager.lend(n); }

    /**
     * @brief Read some data
     * @param n the size to read
     * @return a view for reading
     * @throw std::out_of_range if not enough data is available
     */
    def read(size_t n) -> read_view { return read_manager.lend(n); }

    /**
     * @brief Read all available data
     * @return a view for reading
     * @note This function will not throw and will return an empty view if no data is available.
     */
    def read() noexcept -> read_view { return read_manager.lend(); }

    /**
     * @brief Asynchronously prepare a space for writing
     * @param n the size to write
     * @return a view for writing
     * @note This function will asynchronously wait until enough space is available.
     */
    boost::asio::awaitable<write_view> async_prepare(size_t n) noexcept {
        while (true) {
            try { co_return prepare(n); }
            catch (std::out_of_range &) { }
            co_await async_sleep(0ms);
        }
    }

    /**
     * @brief Asynchronously read some data
     * @param n the size to read
     * @return a view for reading
     * @note This function will asynchronously wait until enough data is available.
     */
    boost::asio::awaitable<read_view> async_read(size_t n) noexcept {
        while (true) {
            try { co_return read(n); }
            catch (std::out_of_range &) { }
            co_await async_sleep(0ms);
        }
    }

    operator std::string(this auto &&self) noexcept { return std::format("StreamBuffer {{ start = {}, stop = {}, size = {} }}", self.start, self.stop, self.size()); }
    friend auto &operator<<(auto &os, const StreamBuffer<T, N> &buf) { return os << std::string(buf); }

};

#undef def

static_assert(std::random_access_iterator<StreamBuffer<int, 1>::iterator>, "StreamBuffer::iterator must be a random access iterator");
static_assert(std::random_access_iterator<StreamBuffer<int, 1>::const_iterator>, "StreamBuffer::const_iterator must be a random access iterator");
static_assert(std::random_access_iterator<StreamBuffer<int, 1>::reverse_iterator>, "StreamBuffer::reverse_iterator must be a random access iterator");
static_assert(std::random_access_iterator<StreamBuffer<int, 1>::const_reverse_iterator>, "StreamBuffer::const_reverse_iterator must be a random access iterator");
static_assert(std::ranges::random_access_range<StreamBuffer<int, 1>>, "StreamBuffer must be a random access range");
static_assert(std::ranges::sized_range<StreamBuffer<int, 1>>, "StreamBuffer must be a sized range");
static_assert(std::ranges::constant_range<const StreamBuffer<int, 1>>, "const StreamBuffer must be a constant range");
static_assert(!std::ranges::constant_range<StreamBuffer<int, 1>>, "StreamBuffer must not be a constant range");

