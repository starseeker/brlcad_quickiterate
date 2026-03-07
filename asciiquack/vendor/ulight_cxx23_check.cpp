// Compile-time probe used by CMakeLists.txt to test whether the compiler
// supports the C++23 features required to build the embedded µlight library.
//
// If this file compiles successfully with CXX_STANDARD 23, then
// vendor/ulight/ can be built and USE_ULIGHT may be enabled.

#include <expected>         // std::expected              – C++23
#include <source_location>  // std::source_location       – C++20
#include <span>             // std::span                  – C++20
#include <string_view>

// char8_t (C++20)
static_assert(sizeof(char8_t) == 1);

// std::expected (C++23)
auto expected_test() -> std::expected<int, int> { return 42; }

// consteval (C++20)
consteval int ce() { return 1; }
static_assert(ce() == 1);

// if !consteval (C++23)
constexpr int if_not_consteval(int x) {
    if !consteval { return x * 2; }
    return x;
}
static_assert(if_not_consteval(3) == 3);  // called at compile time → not doubled

// static lambda (C++23)
constexpr auto static_lam = [](int x) static noexcept { return x + 1; };
static_assert(static_lam(2) == 3);

int main() {
    auto r = expected_test();
    (void)r;
    return 0;
}
