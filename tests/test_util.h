#pragma once

// Minimal test helpers: no framework, a non-zero exit code means failure.
#include <cstdio>

namespace test {

inline int& Failures() {
    static int failures = 0;
    return failures;
}

inline void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++Failures();
}

inline int Finish() {
    std::printf(Failures() ? "\n%d check(s) failed\n" : "\nall checks passed\n", Failures());
    return Failures() ? 1 : 0;
}

}  // namespace test
