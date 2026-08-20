#pragma once

#include <cstdio>
#include <cstring>
#include <vector>

namespace testing {

struct TestCase { const char* name; void (*fn)(); };

inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline int& failures() { static int f = 0; return f; }

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int runAll() {
    int failedTests = 0;
    for (auto& tc : registry()) {
        const int before = failures();
        tc.fn();
        if (failures() > before) { ++failedTests; printf("FAIL %s\n", tc.name); }
        else                     { printf("ok   %s\n", tc.name); }
    }
    printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}

} // namespace testing

#define TEST(name)                                                   \
    static void name();                                              \
    static ::testing::Registrar reg_##name(#name, name);             \
    static void name()

#define CHECK(cond)                                                  \
    do { if (!(cond)) { ++::testing::failures();                     \
        printf("  %s:%d: CHECK failed: %s\n",                        \
               __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ(a, b)                                               \
    do { const long long _a = (long long)(a);                        \
         const long long _b = (long long)(b);                        \
         if (_a != _b) { ++::testing::failures();                    \
        printf("  %s:%d: CHECK_EQ failed: %s != %s (%lld vs %lld)\n",\
               __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_STREQ(a, b)                                            \
    do { const char* _a = (a); const char* _b = (b);                 \
         if (strcmp(_a, _b) != 0) { ++::testing::failures();         \
        printf("  %s:%d: CHECK_STREQ failed: \"%s\" != \"%s\"\n",    \
               __FILE__, __LINE__, _a, _b); } } while (0)
