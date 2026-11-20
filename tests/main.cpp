// Runs the registered test cases. With an argument, runs only the cases
// whose name contains it, for example: kinetics_tests "Hill"

#include <cstdio>
#include <string>

#include "check.hpp"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int run = 0;
    for (const auto& [name, fn] : test::registry()) {
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        const int before = test::g_failures;
        fn();
        ++run;
        std::printf("[%s] %s\n", test::g_failures == before ? " ok " : "FAIL", name.c_str());
    }
    std::printf("\n%d test cases, %d checks, %d failures\n", run, test::g_checks, test::g_failures);
    return test::g_failures == 0 && run > 0 ? 0 : 1;
}
