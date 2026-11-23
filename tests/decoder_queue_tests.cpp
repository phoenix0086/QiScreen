#include "decoder_queue.h"
#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>
#include <cstdio>
using namespace std::chrono_literals;
struct Packet { std::vector<unsigned char> bytes; int id = 0; };
static void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        qiscreen::DecoderQueue<int,Packet> queue;
        queue.Start(2,4);
        Check(queue.Push({{1,2},1}) && queue.Push({{3,4},2}), "fill queue");
        auto producer = std::async(std::launch::async, [&] { return queue.Push({{5,6},3}); });
        const bool blocked = producer.wait_for(40ms) == std::future_status::timeout;
        queue.AddInput(10); Packet packet; int input;
        Check(queue.Take(input,packet) && packet.id == 1 && input == 10, "oldest reference frame preserved");
        Check(producer.get() && blocked, "producer waits instead of dropping a reference");
        queue.AddInput(11); Check(queue.Take(input,packet) && packet.id == 2, "second reference preserved");
        queue.AddInput(12); Check(queue.Take(input,packet) && packet.id == 3, "new frame keeps order");
        queue.Stop(); queue.Clear(); queue.Start(8,4);
        Check(queue.Push({{1,2,3,4},4}), "byte limit fill");
        auto stoppedProducer = std::async(std::launch::async, [&] { return queue.Push({{5},5}); });
        bool byteBlocked = stoppedProducer.wait_for(40ms) == std::future_status::timeout;
        queue.Stop(); Check(!stoppedProducer.get() && byteBlocked, "stop wakes byte-limited producer");
        queue.Clear(); queue.Start(2,4);
        auto consumer = std::async(std::launch::async, [&] { Packet p; int i; return queue.Take(i,p); });
        queue.Stop(); Check(!consumer.get(), "stop wakes empty consumer");
        queue.Clear(); queue.Start(2,4);
        Check(queue.Depth() == 0 && queue.Push({{7},7}), "restart has no stale packets");
        Check(!queue.Push({{1,2,3,4,5},8}), "oversized packet rejected without eviction");
        queue.AddInput(13); Check(queue.Take(input,packet) && packet.id == 7, "oversize does not corrupt queued data");
        queue.Stop(); puts("PASS: reference-safe bounded queue, producer/consumer shutdown, restart"); return 0;
    } catch (const std::exception &e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
