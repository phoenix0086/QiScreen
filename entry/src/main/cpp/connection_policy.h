#pragma once
#include <atomic>
#include <cstdint>
#include "raop.h"

namespace qiscreen {
class LatestCommand {
public:
    uint64_t Issue() { return ++generation_; }
    bool IsCurrent(uint64_t generation) const { return generation == generation_.load(); }
private:
    std::atomic<uint64_t> generation_{0};
};

inline bool StartWithPreferredPort(raop_t *server, unsigned short preferred, unsigned short &actual) {
    actual = preferred;
    if (raop_start_httpd(server, &actual) >= 0 && actual != 0) return true;
    if (!preferred) return false;
    actual = 0;
    return raop_start_httpd(server, &actual) >= 0 && actual != 0;
}
}
