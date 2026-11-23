#pragma once
#include <chrono>
#include <cstdint>

namespace qiscreen {
inline int64_t MonotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Nonblocking, coalesced notification. No ArkTS calls are made on codec/network threads.
void NotifyUi(bool viewOnly = false);
}
