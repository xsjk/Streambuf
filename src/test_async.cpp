#include <iostream>
#include <streambuf.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

using boost::asio::awaitable;
using namespace boost::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

awaitable<void> async_main();
int main() {
    boost::asio::io_context ctx;
    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { std::printf("KeyboardInterrupt\n"), ctx.stop(); });
    boost::asio::co_spawn(ctx, [&]() -> awaitable<void> { co_await async_main(); ctx.stop(); }, boost::asio::detached);
    ctx.run();
    return 0;
}

awaitable<void> sleep(auto time) {
    co_await boost::asio::steady_timer(co_await boost::asio::this_coro::executor, time).async_wait(boost::asio::use_awaitable);
}

void print(auto &&c) {
    for (const auto &i : c)
        std::cout << i << ' ';
    std::cout << std::endl;
}

awaitable<void> read(auto &&rb, size_t n, std::string msg) {
    auto v = co_await rb.async_read(n);
    std::cout << msg << ' ';
    print(v);
}
awaitable<void> write(auto &&rb, size_t n, auto &&f, std::string msg) {
    auto v = co_await rb.async_prepare(n);
    std::cout << msg << std::endl;
    for (int i = 0; i < n; ++i)
        v[i] = f(i);
}

awaitable<void> async_main() {

    StreamBuffer<int, 15> rb {};

    assert(rb.empty());

    co_await (
        read(rb, 9,                                     "(4)") &&
        write(rb, 4, [](auto i) { return i; },          "(1)") &&
        write(rb, 4, [](auto i) { return i * 2; },      "(2)") &&
        write(rb, 4, [](auto i) { return i * 2 + 1; },  "(3)")
    );
    assert(rb.size() == 3);
    std::cout << std::endl;
    
    co_await (
        write(rb, 10, [](auto i) { return i * i; },     "(1)") &&
        write(rb, 11, [](auto i) { return i * 100 ; },  "(3)") &&
        read(rb, 10,                                    "(2)")
    );
    assert(rb.size() == 14);
    assert(rb.full());
    std::cout << std::endl;

    print(rb);

    co_return;
}
