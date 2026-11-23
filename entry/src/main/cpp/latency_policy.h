#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace qiscreen {
struct LatencySummary { int samples = 0; double p50Ms = -1, p95Ms = -1, maxMs = -1; };
// Caller serializes access. Fixed storage keeps diagnostics from growing with stream duration.
class LatencyWindow {
public:
    void Add(int64_t us) {
        if (us < 0 || us > 30000000) return;
        values_[next_] = us; next_ = (next_ + 1) % values_.size();
        if (count_ < values_.size()) ++count_;
    }
    void Reset() { count_ = 0; next_ = 0; }
    LatencySummary Snapshot() const {
        if (!count_) return {};
        auto copy = values_; std::sort(copy.begin(), copy.begin() + count_);
        const size_t p95 = (count_ * 95 + 99) / 100 - 1;
        return {static_cast<int>(count_), copy[(count_ - 1) / 2] / 1000.0,
                copy[p95] / 1000.0, copy[count_ - 1] / 1000.0};
    }
private:
    std::array<int64_t, 120> values_{};
    size_t count_ = 0, next_ = 0;
};

inline bool SkipObsoleteOutput(bool enabled, int64_t captureUs, int64_t latestCaptureUs,
                               int64_t receivedUs, int64_t nowUs, int64_t lastPresentedUs) {
    if (!enabled || lastPresentedUs <= 0 || captureUs <= 0 || latestCaptureUs <= captureUs) return false;
    // Always show the newest received frame and guarantee visible progress at least every 100 ms.
    if (nowUs < lastPresentedUs || nowUs - lastPresentedUs >= 100000) return false;
    const int64_t lead = latestCaptureUs - captureUs;
    if (lead > 5000000) return false; // Clock discontinuity, not a reliable latency sample.
    // A normal 60 Hz hardware pipeline can be 1–2 frames behind reception. Only catch up
    // when it exceeds three frames; a 16 ms threshold discarded healthy video output.
    return lead > 50000 || (receivedUs > 0 && nowUs - receivedUs > 60000);
}
}
