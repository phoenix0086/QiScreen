#pragma once
#include "tap_tracker.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace qiscreen {
struct ViewPoint { int id = 0; double x = 0, y = 0; };
class ViewGesture {
public:
    double scale = 1, panX = 0, panY = 0;
    void Viewport(double width, double height, double x, double y) {
        if (!std::isfinite(width + height + x + y) || width <= 0 || height <= 0) return;
        if (width != width_ || height != height_) { Reset(); Cancel(); }
        width_ = width; height_ = height; originX_ = x; originY_ = y; Clamp();
    }
    void Content(double width, double height) {
        if (width <= 0 || height <= 0 || !std::isfinite(width + height)) return;
        if (width != contentWidth_ || height != contentHeight_) { Reset(); Cancel(); }
        contentWidth_ = width; contentHeight_ = height; Clamp();
    }
    void Reset() { scale = 1; panX = 0; panY = 0; }
    void SetScale(double value) { Zoom(value, width_/2, height_/2, width_/2, height_/2); }
    void Fill() {
        if (width_ <= 0 || height_ <= 0) return;
        const double fit = std::min(width_/contentWidth_,height_/contentHeight_);
        const double cover = std::max(width_/contentWidth_,height_/contentHeight_);
        Reset(); Cancel(); SetScale(cover/fit);
    }
    void Cancel() { count_ = 0; tap_.Cancel(); multi_ = false; lastTapUs_ = 0; }
    // Coordinates are physical screen pixels, unaffected by the surface's own transform.
    // Return true only for a completed double tap. Zoom reset remains an explicit action.
    bool Touch(const ViewPoint *points, size_t count, ViewPoint changed, int64_t now, bool cancel = false) {
        if (cancel) { Cancel(); return false; }
        if (count > 2) count = 2;
        ViewPoint p[2];
        for (size_t i=0;i<count;++i) p[i] = {points[i].id, points[i].x-originX_, points[i].y-originY_};
        changed.x -= originX_; changed.y -= originY_;
        bool tap = false;
        if (count == 2) {
            tap_.Cancel(); multi_ = true; lastTapUs_ = 0;
            if (p[0].id > p[1].id) std::swap(p[0],p[1]);
            if (count_ == 2 && p[0].id == prior_[0].id && p[1].id == prior_[1].id) {
                double before = std::hypot(prior_[0].x-prior_[1].x,prior_[0].y-prior_[1].y);
                double after = std::hypot(p[0].x-p[1].x,p[0].y-p[1].y);
                if (before > 1 && after > 1) Zoom(scale*after/before,
                    (prior_[0].x+prior_[1].x)/2,(prior_[0].y+prior_[1].y)/2,
                    (p[0].x+p[1].x)/2,(p[0].y+p[1].y)/2);
            }
        } else if (count == 1) {
            if (count_ == 0) { multi_ = false; tap_.Down(p[0].x,p[0].y,now); }
            else if (count_ == 1 && p[0].id == prior_[0].id) {
                tap_.Move(p[0].x,p[0].y);
                if (scale > 1) { panX += p[0].x-prior_[0].x; panY += p[0].y-prior_[0].y; Clamp(); }
            } else { tap_.Cancel(); multi_ = true; }
        } else if (count_ == 1 && !multi_) {
            if (tap_.Up(changed.x,changed.y,now)) {
                if (lastTapUs_ > 0 && now-lastTapUs_ >= 0 && now-lastTapUs_ <= 350000 &&
                    std::hypot(changed.x-lastTap_.x,changed.y-lastTap_.y) <= 80) {
                    tap = true; lastTapUs_ = 0;
                } else { lastTap_ = changed; lastTapUs_ = now; }
            } else lastTapUs_ = 0;
        }
        if (!count) { tap_.Cancel(); multi_ = false; }
        count_ = count; for(size_t i=0;i<count;++i) prior_[i]=p[i];
        return tap;
    }
private:
    void Zoom(double value, double oldX, double oldY, double newX, double newY) {
        if (!std::isfinite(value) || width_ <= 0 || height_ <= 0) return;
        double next = std::clamp(value,1.0,4.0);
        panX = newX-width_/2-(oldX-width_/2-panX)*next/scale;
        panY = newY-height_/2-(oldY-height_/2-panY)*next/scale;
        scale = next; Clamp();
    }
    void Clamp() {
        const double fit = std::min(width_/contentWidth_,height_/contentHeight_);
        const double maxX = std::max(0.0,(contentWidth_*fit*scale-width_)/2);
        const double maxY = std::max(0.0,(contentHeight_*fit*scale-height_)/2);
        panX = std::clamp(panX,-maxX,maxX); panY = std::clamp(panY,-maxY,maxY);
    }
    double width_ = 0,height_ = 0,originX_ = 0,originY_ = 0,contentWidth_ = 1920,contentHeight_ = 1080;
    size_t count_ = 0;
    ViewPoint prior_[2],lastTap_;
    bool multi_ = false;
    int64_t lastTapUs_ = 0;
    TapTracker tap_;
};
}
