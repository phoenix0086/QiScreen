#pragma once
#include <cstdint>

namespace qiscreen {
class TapTracker {
public:
    void Down(float x, float y, int64_t nowUs) { x_ = x; y_ = y; downUs_ = nowUs; active_ = true; moved_ = false; }
    void Move(float x, float y) { if (active_ && Distance(x,y) > 1600) moved_ = true; }
    bool Up(float x, float y, int64_t nowUs) {
        const bool tap = active_ && !moved_ && nowUs >= downUs_ && nowUs - downUs_ <= 300000 && Distance(x,y) <= 1600;
        active_ = false; return tap;
    }
    void Cancel() { active_ = false; }
private:
    float Distance(float x,float y) const { return (x-x_)*(x-x_)+(y-y_)*(y-y_); }
    float x_ = 0, y_ = 0;
    int64_t downUs_ = 0;
    bool active_ = false, moved_ = false;
};
}
