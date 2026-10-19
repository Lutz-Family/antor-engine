#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

namespace antor::test {

inline void expect(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " " + expression);
    }
}

inline int run_test(const char* name, void (*fn)()) {
    try {
        fn();
        std::cout << "ok " << name << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL " << name << ": " << ex.what() << '\n';
        return 1;
    }
}

} // namespace antor::test

#define EXPECT(condition)                                                                          \
    ::antor::test::expect(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
