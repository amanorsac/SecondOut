#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include <functional>

// Minimal test framework — no dependencies.
struct TestCase { std::string name; std::function<void()> fn; };

inline std::vector<TestCase>& allTests()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount()
{
    static int failures = 0;
    return failures;
}

struct TestRegistrar
{
    TestRegistrar (const char* name, std::function<void()> fn)
    {
        allTests().push_back ({ name, std::move (fn) });
    }
};

#define TEST_CASE(name) \
    static void name(); \
    static TestRegistrar registrar_##name (#name, name); \
    static void name()

#define EXPECT(cond) \
    do { if (! (cond)) { \
        std::printf ("  FAIL: %s (line %d): %s\n", __FILE__, __LINE__, #cond); \
        ++failureCount(); \
    } } while (false)

#define EXPECT_NEAR(a, b, tol) \
    do { const double va_ = (a), vb_ = (b); if (! (va_ > vb_ - (tol) && va_ < vb_ + (tol))) { \
        std::printf ("  FAIL: %s (line %d): %s (=%f) not within %f of %s (=%f)\n", \
                     __FILE__, __LINE__, #a, va_, (double) (tol), #b, vb_); \
        ++failureCount(); \
    } } while (false)
