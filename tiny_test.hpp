#ifndef TINY_TEST_HPP
#define TINY_TEST_HPP

#include <iostream>
#include <string>
#include <vector>
#include <exception>

#if defined(__GNUC__) || defined(__clang__)
# define TT_UNUSED __attribute__((unused))
#else
# define TT_UNUSED
#endif

namespace TinyTest {

struct AssertionFailure {
    std::string file;
    int line;
    std::string message;

    AssertionFailure(const std::string& f, int l, const std::string& m)
        : file(f), line(l), message(m) {}
};

typedef void (*TestFunction)();

struct TestCase {
    std::string name;
    TestFunction func;

    TestCase(const std::string& n, TestFunction f) : name(n), func(f) {}
};

class Registry {
private:
    std::vector<TestCase> tests_;

    Registry() {}
    Registry(const Registry&);
    Registry& operator=(const Registry&);

public:
    static Registry& instance() {
        static Registry inst;
        return inst;
    }

    void add(const std::string& name, TestFunction func) {
        tests_.push_back(TestCase(name, func));
    }

    int runAll() {
        int passed = 0;
        int failed = 0;

        std::cout << "\033[1;34m=== RUNNING " << tests_.size() << " TEST(S) ===\033[0m\n\n";

        for (size_t i = 0; i < tests_.size(); ++i) {
            std::cout << "\033[1m[ RUN      ] \033[0m" << tests_[i].name << "\n";
            try {
                tests_[i].func();
                std::cout << "\033[1;32m[     PASS ] \033[0m" << tests_[i].name << "\n";
                ++passed;
            } catch (const AssertionFailure& af) {
                std::cout << "\033[1;31m[     FAIL ] \033[0m" << tests_[i].name
                          << "\n  \033[33m" << af.file << ":" << af.line << "\033[0m: "
                          << af.message << "\n";
                ++failed;
            } catch (const std::exception& e) {
                std::cout << "\033[1;31m[     FAIL ] \033[0m" << tests_[i].name
                          << " (Unhandled exception: " << e.what() << ")\n";
                ++failed;
            } catch (...) {
                std::cout << "\033[1;31m[     FAIL ] \033[0m" << tests_[i].name
                          << " (Unhandled non-standard exception)\n";
                ++failed;
            }
        }

        std::cout << "\n\033[1;34m=== RESULTS ===\033[0m\n";
        std::cout << "Total:  " << tests_.size() << "\n";
        std::cout << "\033[1;32mPassed: " << passed << "\033[0m\n";
        if (failed > 0) {
            std::cout << "\033[1;31mFailed: " << failed << "\033[0m\n";
            return 1;
        }
        std::cout << "Failed: 0\n";
        return 0;
    }
};

struct AutoRegister {
    AutoRegister(const std::string& name, TestFunction func) {
        Registry::instance().add(name, func);
    }
};

} // namespace TinyTest

// --- Test Definition & Runner Macros ---

#define TEST(test_name) \
    static void test_name(); \
    namespace { \
        static TT_UNUSED TinyTest::AutoRegister reg_##test_name(#test_name, &test_name); \
    } \
    static void test_name()

#define RUN_ALL_TESTS() TinyTest::Registry::instance().runAll()

// --- Assertion Macros ---

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, "Expected true: " #condition); \
        } \
    } while (false)

#define ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, "Expected false: " #condition); \
        } \
    } while (false)

#define ASSERT_EQ(actual, expected) \
    do { \
        if (!((actual) == (expected))) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, \
                "Equality check failed: " #actual " == " #expected); \
        } \
    } while (false)

#define ASSERT_NE(actual, expected) \
    do { \
        if ((actual) == (expected)) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, \
                "Inequality check failed: " #actual " != " #expected); \
        } \
    } while (false)

#define ASSERT_THROWS(expression, ExceptionType) \
    do { \
        bool caught_ = false; \
        try { \
            expression; \
        } catch (const ExceptionType&) { \
            caught_ = true; \
        } catch (...) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, \
                "Expected " #ExceptionType ", but caught an unexpected exception type"); \
        } \
        if (!caught_) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, \
                "Expected " #ExceptionType " to be thrown, but nothing was thrown"); \
        } \
    } while (false)

#define ASSERT_NO_THROW(expression) \
    do { \
        try { \
            expression; \
        } catch (const std::exception& e) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, \
                std::string("Expected no exception, but caught: ") + e.what()); \
        } catch (...) { \
            throw TinyTest::AssertionFailure(__FILE__, __LINE__, \
                "Expected no exception, but an unknown exception was thrown"); \
        } \
    } while (false)

#endif // TINY_TEST_HPP