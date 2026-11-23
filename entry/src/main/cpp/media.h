#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avbuffer.h>
#include <native_window/external_window.h>
#include <ohaudio/native_audiorenderer.h>
#include "latency_policy.h"
#include "decoder_queue.h"
#include "pcm_ring.h"

namespace qiscreen {
struct PipelineDiagnostics { LatencySummary queue, decode, sourceAge; };
struct MediaStats {
    std::atomic<uint64_t> frames{0}, dropped{0}, audioFrames{0};
    std::atomic<int> width{1920}, height{1080};
    std::atomic<int> targetFps{60}, requestedWidth{1920}, requestedHeight{1080};
    std::atomic<bool> hardwareVideo{false}, hevc{false};
    std::atomic<uint64_t> receivedBytes{0};
    std::atomic<int64_t> controlConnectedUs{0}, firstFrameAtUs{0};
    std::atomic<int64_t> serviceReadyMs{-1}, decoderSetupMs{-1}, firstFrameMs{-1};
    std::atomic<bool> lowLatency{true};
    std::atomic<uint64_t> received{0}, decoded{0}, skippedOutputs{0}, timingMatches{0};
    std::atomic<int> queuedFrames{0}, queuePeak{0};
    std::atomic<int64_t> ntpRttUs{-1}, lastPerfLogUs{0};
    std::mutex metricsMutex;
    LatencyWindow queueLatency, decodeLatency, sourceAge;
    std::mutex errorMutex;
    std::string error;
    void Error(const std::string &message);
    void ResetLatencyWindows();
    void ResetDiagnostics();
    PipelineDiagnostics Diagnostics();
};

// Only Start/Stop own the codec lifetime. Callbacks never destroy their codec.
class MediaDecoder {
public:
    MediaDecoder(bool video, MediaStats &stats) : video_(video), stats_(stats) {}
    ~MediaDecoder() { Stop(); }
    static bool SupportsHevc(int width, int height, int fps);
    bool StartVideo(OHNativeWindow *window, int width, int height, bool hevc = false);
    void Interrupt();
    bool StartAudio(uint8_t compression);
    void Submit(const uint8_t *data, size_t size, int64_t pts, int64_t captureUs = 0);
    void Stop();
    void SetVolume(float volume);
private:
    struct Input { uint32_t index; OH_AVBuffer *buffer; };
    struct Packet { std::vector<uint8_t> bytes; int64_t pts, receivedUs, captureUs; };
    struct Flight { int64_t pts, receivedUs, submittedUs, captureUs; };
    bool StartCodec(OH_AVFormat *format, OHNativeWindow *window);
    void StopLocked();
    void Feed();
    static void OnError(OH_AVCodec *, int32_t, void *);
    static void OnFormat(OH_AVCodec *, OH_AVFormat *, void *);
    static void OnInput(OH_AVCodec *, uint32_t, OH_AVBuffer *, void *);
    static void OnOutput(OH_AVCodec *, uint32_t, OH_AVBuffer *, void *);
    static int32_t WriteAudio(OH_AudioRenderer *, void *, void *, int32_t);
    bool CreateRenderer();
    bool video_;
    MediaStats &stats_;
    OH_AVCodec *codec_ = nullptr;
    OH_AudioRenderer *renderer_ = nullptr;
    std::mutex lifecycle_, pcmMutex_, flightMutex_;
    DecoderQueue<Input,Packet> queue_;
    PcmRing pcm_;
    std::deque<Flight> flights_;
    std::atomic<int64_t> latestCaptureUs_{0}, lastPresentedUs_{0};
    std::atomic<bool> running_{false};
    std::atomic<float> volume_{1.0f};
    std::thread feeder_;
};
}
