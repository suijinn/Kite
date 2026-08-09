// Kite - a very small test harness.
//
// Deliberately dependency-free: the project has no package manager wired up and
// pulling one in for a handful of assertions would cost more than it saves.
// Tests self-register, `--filter` selects a subset, and CTest drives one entry
// per suite.
#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kite::test {

struct TestCase {
    std::string suite;
    std::string name;
    void (*body)() = nullptr;
};

int Register(const char* suite, const char* name, void (*body)());

[[noreturn]] void Fail(const char* file, int line, const std::string& message);

template <typename T>
std::string ToText(const T& value) {
    using Bare = std::decay_t<T>;
    if constexpr (std::is_same_v<Bare, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_enum_v<Bare>) {
        return std::to_string(static_cast<long long>(value));
    } else if constexpr (std::is_arithmetic_v<Bare>) {
        return std::to_string(value);
    } else if constexpr (std::is_constructible_v<std::string, const T&>) {
        return std::string(value);
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        return std::string(std::string_view(value));
    } else {
        return "<value>";
    }
}

}  // namespace kite::test

#define KITE_FAIL(message) ::kite::test::Fail(__FILE__, __LINE__, (message))

#define KITE_EXPECT(condition)                                                                 \
    do {                                                                                       \
        if (!(condition)) KITE_FAIL(std::string("expected true: ") + #condition);               \
    } while (false)

#define KITE_EXPECT_FALSE(condition)                                                           \
    do {                                                                                       \
        if ((condition)) KITE_FAIL(std::string("expected false: ") + #condition);               \
    } while (false)

// The operands are copied rather than bound with auto&&. Lifetime extension
// does not reach through a member call, so `container().front()` would leave a
// dangling reference - a trap that silently corrupts comparisons.
#define KITE_EXPECT_EQ(actual, expected)                                                       \
    do {                                                                                       \
        const auto kite_actual_ = (actual);                                                    \
        const auto kite_expected_ = (expected);                                                \
        if (!(kite_actual_ == kite_expected_)) {                                               \
            KITE_FAIL(std::string(#actual) + " == " + #expected +                              \
                      "\n        actual:   " + ::kite::test::ToText(kite_actual_) +            \
                      "\n        expected: " + ::kite::test::ToText(kite_expected_));          \
        }                                                                                      \
    } while (false)

#define KITE_EXPECT_NE(actual, expected)                                                       \
    do {                                                                                       \
        const auto kite_actual_ = (actual);                                                    \
        const auto kite_expected_ = (expected);                                                \
        if ((kite_actual_ == kite_expected_)) {                                                \
            KITE_FAIL(std::string(#actual) + " != " + #expected + " (both " +                  \
                      ::kite::test::ToText(kite_actual_) + ")");                               \
        }                                                                                      \
    } while (false)

#define KITE_EXPECT_NEAR(actual, expected, tolerance)                                          \
    do {                                                                                       \
        const double kite_a_ = static_cast<double>(actual);                                    \
        const double kite_b_ = static_cast<double>(expected);                                  \
        if (!((kite_a_ - kite_b_) < (tolerance) && (kite_b_ - kite_a_) < (tolerance))) {       \
            KITE_FAIL(std::string(#actual) + " ~= " + #expected +                              \
                      "\n        actual:   " + ::kite::test::ToText(kite_a_) +                 \
                      "\n        expected: " + ::kite::test::ToText(kite_b_));                 \
        }                                                                                      \
    } while (false)

#define KITE_TEST(suite, name)                                                                 \
    static void kite_test_body_##suite##_##name();                                             \
    static const int kite_test_reg_##suite##_##name [[maybe_unused]] =                          \
        ::kite::test::Register(#suite, #name, &kite_test_body_##suite##_##name);                \
    static void kite_test_body_##suite##_##name()
