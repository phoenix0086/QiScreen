#pragma once
#include "media.h"
extern "C" {
#include "raop.h"
#include "dnssd.h"
}
#include "connection_policy.h"

namespace qiscreen {
class Receiver {
public:
    static Receiver &Instance();
    uint64_t NewCommand() { return commands_.Issue(); }
    bool Start(const std::string &name, const std::string &filesDir, int width, int height, int fps, uint64_t command, bool highQuality);
    bool Stop(uint64_t command);
    void SetSurface(OHNativeWindow *window);
    MediaStats stats;
    std::atomic<bool> listening{false}, streaming{false}, surfaceReady{false};
    std::atomic<int> port{0};
    std::mutex clientMutex;
    std::string client;
    MediaDecoder video{true, stats}, audio{false, stats};
private:
    Receiver() = default;
    void StopLocked();
    std::mutex lifecycle_, mediaStartMutex_;
    bool acceptingMedia_ = false;
    LatestCommand commands_;
    OHNativeWindow *window_ = nullptr;
    raop_t *raop_ = nullptr;
    dnssd_t *dnssd_ = nullptr;
    std::atomic<int> connections_{0};
    static void Log(void *, int, const char *);
    static int VideoFormat(void *, video_codec_t, unsigned short, unsigned short, unsigned short,
                           unsigned short, unsigned short, unsigned short);
    static void VideoProcess(void *, raop_ntp_t *, video_decode_struct *);
    static void AudioProcess(void *, raop_ntp_t *, audio_decode_struct *);
    static void AudioFormat(void *, unsigned char *, unsigned short *, bool *, bool *, uint64_t *);
    static void VideoReset(void *, reset_type_t);
    static void ClientRequest(void *, char *, char *, char *, bool *);
};
}
