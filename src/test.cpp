#include <streambuf.hpp>

#include <memory>
#include <vector>
#include <span>
#include <string>
#include <iostream>
#include <ranges>

template <typename T>
consteval auto get_type_name() {
    using namespace std::string_view_literals;
    #if defined(__clang__) || defined(__GNUC__)
        constexpr auto prefix = "T = "sv;
        constexpr auto suffix = "]"sv;
        constexpr std::string_view function = __PRETTY_FUNCTION__;
    #elif defined(_MSC_VER)
        constexpr auto prefix = "get_type_name<"sv;
        constexpr auto suffix = ">(void)"sv;
        constexpr std::string_view function = __FUNCSIG__;
    #else
        #error Unsupported compiler
    #endif
    constexpr auto start = function.find(prefix) + prefix.size();
    return function.substr(start, function.rfind(suffix) - start);
}

constexpr auto get_type_name(auto &&t) {
    return get_type_name<decltype(t)>();
}

void f(auto &&s) {
    std::cout << get_type_name<decltype(s)>() << '\n'
              << "input range: " << std::ranges::input_range<decltype(s)> << '\n'
              << "forward range: " << std::ranges::forward_range<decltype(s)> << '\n'
              << "bidirectional range: " << std::ranges::bidirectional_range<decltype(s)> << '\n'
              << "random access range: " << std::ranges::random_access_range<decltype(s)> << '\n'
              << "contiguous range: " << std::ranges::contiguous_range<decltype(s)> << '\n'
              << "begin()\t\t" << get_type_name(*s.begin()) << '\t' << get_type_name(s.begin()) << '\n'
              << "cbegin()\t" << get_type_name(*s.cbegin()) << '\t' << get_type_name(s.cbegin()) << '\n'
              << "rbegin()\t" << get_type_name(*s.rbegin()) << '\t' << get_type_name(s.rbegin()) << '\n'
              << "crbegin()\t" << get_type_name(*s.crbegin()) << '\t' << get_type_name(s.crbegin()) << '\n'
              << "front()\t\t" << get_type_name(s.front()) << ' ' << s.front() << '\n'
              << "back()\t\t" << get_type_name(s.back()) << ' ' << s.back() << '\n'
              << "operator[]\t" << get_type_name(s[0]) << ' ' << s[0] << '\n'
              << "size()\t\t" << get_type_name(s.size()) << ' ' << s.size() << '\n'
              << "empty()\t\t" << get_type_name(s.empty()) << ' ' << s.empty() << '\n'
              << "operator bool()\t" << get_type_name(bool(s)) << ' ' << bool(s) << '\n'
    << std::endl;
}

bool run(auto &&f) {
    try {
        f();
        return true;
    } catch (std::exception &) {
        return false;
    }
}

#include <cassert>
int main() {

    StreamBuffer<int, 11> rb{};

    assert(rb.empty());
    assert(run([&](){
        auto v = rb.prepare(5);
        for (int i = 0; i < 5; ++i)
            v[i] = i;
    }) == true);
    assert(rb.size() == 5);
    assert(run([&](){
        auto v = rb.prepare(5);
        for (int i = 0; i < 5; ++i)
            v[i] = i + 100;
    }) == true);
    assert(rb.size() == 10);
    assert(rb.full());
    assert(run([&](){
        auto v = rb.prepare(1);
    }) == false);
    assert(run([&](){
        auto v = rb.read(10);
        for (int i = 0; i < 5; ++i)
            assert(v[i] == i);
        for (int i = 5; i < 10; ++i)
            assert(v[i] == i + 95);
    }) == true);
    assert(rb.size() == 0);
    assert(run([&](){
        auto v = rb.read(1);
    }) == false);

    return 0;
}
