#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace qiscreen {
// Compressed frames may reference earlier frames. Apply backpressure, never evict them.
// Stop must run before joining a protocol producer, so a full queue cannot deadlock shutdown.
template<class Input, class Packet> class DecoderQueue {
public:
    void Start(size_t packets, size_t bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        inputs_.clear(); packets_.clear(); bytes_ = 0;
        maxPackets_ = packets; maxBytes_ = bytes; running_ = true;
    }
    void Stop() {
        { std::lock_guard<std::mutex> lock(mutex_); running_ = false; }
        cv_.notify_all();
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        inputs_.clear(); packets_.clear(); bytes_ = 0;
    }
    bool Push(Packet packet) {
        std::unique_lock<std::mutex> lock(mutex_);
        const size_t bytes = packet.bytes.size();
        if (bytes > maxBytes_) return false;
        cv_.wait(lock, [&] { return !running_ ||
            (packets_.size() < maxPackets_ && bytes_ + bytes <= maxBytes_); });
        if (!running_) return false;
        bytes_ += bytes; packets_.push_back(std::move(packet));
        lock.unlock(); cv_.notify_all(); return true;
    }
    void AddInput(Input input) {
        { std::lock_guard<std::mutex> lock(mutex_);
          if (!running_) return;
          inputs_.push_back(input); }
        cv_.notify_all();
    }
    bool Take(Input &input, Packet &packet) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return !running_ || (!inputs_.empty() && !packets_.empty()); });
        if (!running_) return false;
        input = inputs_.front(); inputs_.pop_front();
        packet = std::move(packets_.front()); packets_.pop_front(); bytes_ -= packet.bytes.size();
        lock.unlock(); cv_.notify_all(); return true;
    }
    size_t Depth() const { std::lock_guard<std::mutex> lock(mutex_); return packets_.size(); }
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Input> inputs_;
    std::deque<Packet> packets_;
    size_t bytes_ = 0, maxPackets_ = 1, maxBytes_ = 1;
    bool running_ = false;
};
}
