#include "receiver.h"
#include "state_events.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <openssl/rand.h>
#include <hilog/log.h>

namespace qiscreen {
Receiver &Receiver::Instance() { static Receiver receiver; return receiver; }
void Receiver::Log(void *, int level, const char *message) {
    // Do not write protocol payloads or pairing material to system logs.
    if (level <= LOGGER_ERR) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD004, "QiScreenProtocol", "%{public}s", message);
}
bool Receiver::Start(const std::string &name, const std::string &filesDir, int width, int height, int fps, uint64_t command, bool highQuality) {
    std::lock_guard<std::mutex> lock(lifecycle_);
    if (!commands_.IsCurrent(command)) return false;
    const int64_t started = MonotonicUs();
    StopLocked();
    { std::lock_guard<std::mutex> errorLock(stats.errorMutex); stats.error.clear(); }
    stats.frames = 0; stats.dropped = 0; stats.audioFrames = 0;
    stats.ResetDiagnostics();
    stats.targetFps = fps; stats.controlConnectedUs = 0; stats.firstFrameAtUs = 0;
    stats.serviceReadyMs = -1; stats.decoderSetupMs = -1; stats.firstFrameMs = -1;
    if (!window_) { stats.Error("画面尚未准备好，请重试"); return false; }
    if (name.empty() || name.size() > 60 || filesDir.empty() ||
        !((width == 3840 && height == 2160) || (width == 2560 && height == 1440) || (width == 1920 && height == 1080) || (width == 1280 && height == 720)) ||
        (fps != 30 && fps != 60)) {
        stats.Error("接收参数无效"); return false;
    }
    uint8_t identity[6]{};
    std::string idPath = filesDir + "/receiver-id";
    std::ifstream idFile(idPath, std::ios::binary);
    idFile.read(reinterpret_cast<char *>(identity), sizeof(identity));
    if (idFile.gcount() != sizeof(identity)) {
        if (RAND_bytes(identity, sizeof(identity)) != 1) { stats.Error("无法生成设备标识"); return false; }
        identity[0] = (identity[0] & 0xfe) | 0x02;
        std::ofstream output(idPath, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(identity), sizeof(identity)); output.close();
        if (!output) { stats.Error("无法保存设备标识"); return false; }
        chmod(idPath.c_str(), 0600);
    }
    char deviceId[18]{};
    std::snprintf(deviceId, sizeof(deviceId), "%02x:%02x:%02x:%02x:%02x:%02x", identity[0], identity[1], identity[2], identity[3], identity[4], identity[5]);
    int error = 0;
    dnssd_ = dnssd_init(name.c_str(), name.size(), reinterpret_cast<char *>(identity), sizeof(identity), 0, &error);
    if (!dnssd_ || error) { stats.Error("设备发现服务初始化失败"); StopLocked(); return false; }
    stats.requestedWidth = width; stats.requestedHeight = height;
    const bool hevc = highQuality && MediaDecoder::SupportsHevc(width,height,fps);
    dnssd_set_airplay_features(dnssd_, 42, hevc ? 1 : 0);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "HEVC advertised=%{public}d", hevc ? 1 : 0);
    dnssd_set_airplay_features(dnssd_, 0, 0);  // No standalone HTTP/HLS video player.
    dnssd_set_airplay_features(dnssd_, 4, 0);
    raop_callbacks_t cb{}; cb.cls = this;
    cb.video_process = VideoProcess; cb.audio_process = AudioProcess;
    cb.video_set_codec_and_size = VideoFormat; cb.audio_get_format = AudioFormat;
    cb.video_reset = VideoReset;
    cb.conn_init = [](void *p) {
        auto *r = static_cast<Receiver *>(p);
        if (r->connections_.fetch_add(1) == 0) r->stats.controlConnectedUs = MonotonicUs();
    };
    cb.conn_destroy = [](void *p) {
        auto *r = static_cast<Receiver *>(p);
        if (--r->connections_ <= 0) { r->streaming = false; NotifyUi(); }
    };
    cb.conn_feedback = [](void *) {};
    cb.conn_reset = [](void *p, int) { static_cast<Receiver *>(p)->streaming = false; NotifyUi(); };
    cb.video_pause = [](void *) {};
    cb.video_resume = [](void *) {};
    cb.audio_flush = [](void *) {};
    cb.video_flush = [](void *) {};
    cb.mirror_video_running = [](void *p, bool value) {
        auto *r = static_cast<Receiver *>(p);
        if (value) { r->stats.frames = 0; r->stats.firstFrameAtUs = 0; r->stats.firstFrameMs = -1; r->stats.ResetDiagnostics(); }
        r->streaming = value; NotifyUi();
    };
    cb.audio_set_client_volume = [](void *) -> double { return 0.0; };
    cb.audio_set_volume = [](void *p, float db) {
        static_cast<Receiver *>(p)->audio.SetVolume(db <= -144.0f ? 0.0f : std::pow(10.0f, db / 20.0f));
    };
    cb.audio_set_metadata = [](void *, const void *, int) {};
    cb.audio_set_coverart = [](void *, const void *, int) {};
    cb.audio_stop_coverart_rendering = [](void *) {};
    cb.audio_set_progress = [](void *, uint32_t *, uint32_t *, uint32_t *) {};
    cb.audio_remote_control_id = [](void *, const char *, const char *) {};
    cb.report_client_request = ClientRequest;
    cb.display_pin = [](void *, char *) {};
    cb.register_client = [](void *, const char *, const char *, const char *) {};
    cb.check_register = [](void *, const char *) -> bool { return false; };
    cb.passwd = [](void *, int *length) -> const char * { *length = 0; return nullptr; };
    cb.export_dacp = [](void *, const char *, const char *) {};
    cb.on_video_play = [](void *p, const char *, float) { static_cast<Receiver *>(p)->stats.Error("请使用 Mac 的屏幕镜像／扩展显示器功能"); };
    cb.on_video_scrub = [](void *, float) {};
    cb.on_video_rate = [](void *, float) {};
    cb.on_video_stop = [](void *) {};
    cb.on_video_playlist_remove = [](void *) -> float { return 0.0f; };
    cb.on_video_acquire_playback_info = [](void *, playback_info_t *info) { std::memset(info, 0, sizeof(*info)); };
    { std::lock_guard<std::mutex> lock(mediaStartMutex_); acceptingMedia_ = true; }
    ntp_global_init();
    raop_ = raop_init(&cb);
    if (!raop_) { stats.Error("协议服务初始化失败"); StopLocked(); return false; }
    raop_set_log_callback(raop_, Log, this); raop_set_log_level(raop_, LOGGER_ERR);
    std::string keyPath = filesDir + "/airplay-key.pem";
    if (raop_init2(raop_, 0, deviceId, keyPath.c_str())) { stats.Error("配对密钥初始化失败"); StopLocked(); return false; }
    chmod(keyPath.c_str(), 0600);
    raop_set_plist(raop_, "width", width); raop_set_plist(raop_, "height", height);
    raop_set_plist(raop_, "maxFPS", fps); raop_set_plist(raop_, "refreshRate", fps);
    raop_set_plist(raop_, "hls", 0);
    unsigned short tcp[2]{0, 0}, udp[3]{0, 0, 0};
    raop_set_tcp_ports(raop_, tcp); raop_set_udp_ports(raop_, udp);
    raop_set_dnssd(raop_, dnssd_);
    // Keep Bonjour's cached SRV endpoint valid across stop/start and process restarts.
    const std::string portPath = filesDir + "/receiver-port";
    unsigned int savedPort = 49152;
    { std::ifstream input(portPath); unsigned int value = 0;
      if (input >> value && value >= 1024 && value <= 65535) savedPort = value; }
    unsigned short actualPort = 0;
    if (!StartWithPreferredPort(raop_, static_cast<unsigned short>(savedPort), actualPort)) {
        stats.Error("无法启动接收端口"); StopLocked(); return false;
    }
    raop_set_port(raop_, actualPort);
    { std::ofstream output(portPath, std::ios::trunc); output << actualPort << '\n'; }
    chmod(portPath.c_str(), 0600);
    if (!commands_.IsCurrent(command)) { StopLocked(); NotifyUi(); return false; }
    if (dnssd_register_raop(dnssd_, actualPort) || dnssd_register_airplay(dnssd_, actualPort)) {
        stats.Error("无法发布接收服务，请检查局域网连接"); StopLocked(); return false;
    }
    stats.width = width; stats.height = height; port = actualPort; listening = true;
    stats.serviceReadyMs = (MonotonicUs() - started) / 1000;
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "Receiver ready: %{public}s, port %{public}u, startup %{public}lld ms",
        name.c_str(), actualPort, static_cast<long long>(stats.serviceReadyMs.load()));
    NotifyUi();
    return true;
}
bool Receiver::Stop(uint64_t command) {
    std::lock_guard<std::mutex> lock(lifecycle_);
    if (!commands_.IsCurrent(command)) return false;
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "Explicit stop requested");
    const int64_t started = MonotonicUs();
    StopLocked();
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "Receiver stopped in %{public}lld ms", static_cast<long long>((MonotonicUs() - started) / 1000));
    NotifyUi(); return true;
}
void Receiver::StopLocked() {
    listening = false;
    if (dnssd_) { dnssd_unregister_raop(dnssd_); dnssd_unregister_airplay(dnssd_); }
    // Wake any producer blocked on a full decoder queue before joining protocol workers.
    { std::lock_guard<std::mutex> lock(mediaStartMutex_);
      acceptingMedia_ = false; video.Interrupt(); audio.Interrupt(); }
    // Destroy protocol workers before their media callbacks and dnssd data.
    if (raop_) { raop_destroy(raop_); raop_ = nullptr; }
    video.Stop(); audio.Stop();
    if (dnssd_) { dnssd_destroy(dnssd_); dnssd_ = nullptr; }
    streaming = false; connections_ = 0; port = 0;
    { std::lock_guard<std::mutex> clientLock(clientMutex); client.clear(); }
}
void Receiver::SetSurface(OHNativeWindow *window) {
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "Surface %{public}s", window ? "created" : "destroyed");
    // Invalidate work still waiting in the NAPI pool before waiting for the lifecycle lock.
    if (!window) commands_.Issue();
    std::lock_guard<std::mutex> lock(lifecycle_);
    if (!window) StopLocked();
    window_ = window; surfaceReady = window != nullptr;
    NotifyUi();
}
int Receiver::VideoFormat(void *p, video_codec_t codec, unsigned short sw, unsigned short sh,
                          unsigned short, unsigned short, unsigned short, unsigned short) {
    auto *r = static_cast<Receiver *>(p);
    std::lock_guard<std::mutex> lock(r->mediaStartMutex_);
    if (!r->acceptingMedia_) return -1;
    if (codec != VIDEO_CODEC_H264 && codec != VIDEO_CODEC_H265) { r->stats.Error("不支持的视频格式，请重新连接"); return -1; }
    return r->video.StartVideo(r->window_, sw, sh, codec == VIDEO_CODEC_H265) ? 0 : -1;
}
void Receiver::VideoProcess(void *p, raop_ntp_t *ntp, video_decode_struct *data) {
    auto *r = static_cast<Receiver *>(p);
    if (data && data->data_len > 0) {
        if (ntp) {
            int64_t offset = 0; double rtt = 0;
            raop_ntp_get_sync_params(ntp, &offset, &rtt);
            if (rtt > 0 && rtt < 5) {
                r->stats.ntpRttUs = static_cast<int64_t>(rtt * 1000000);
                const int64_t age = (static_cast<int64_t>(get_local_time()) - static_cast<int64_t>(data->ntp_time_local)) / 1000;
                if (data->ntp_time_local && age >= 0 && age < 5000000) {
                    std::lock_guard<std::mutex> lock(r->stats.metricsMutex); r->stats.sourceAge.Add(age);
                }
            }
        }
        r->video.Submit(data->data, data->data_len, data->ntp_time_local / 1000, data->ntp_time_remote / 1000);
    }
}
void Receiver::AudioProcess(void *p, raop_ntp_t *, audio_decode_struct *data) {
    if (data && data->data_len > 0) static_cast<Receiver *>(p)->audio.Submit(data->data, data->data_len, data->ntp_time_local / 1000);
}
void Receiver::AudioFormat(void *p, unsigned char *ct, unsigned short *, bool *, bool *, uint64_t *) {
    auto *r = static_cast<Receiver *>(p);
    std::lock_guard<std::mutex> lock(r->mediaStartMutex_);
    if (r->acceptingMedia_) r->audio.StartAudio(*ct);
}
void Receiver::VideoReset(void *p, reset_type_t) {
    auto *r = static_cast<Receiver *>(p);
    r->streaming = false; r->video.Stop(); r->audio.Stop();
    NotifyUi();
}
void Receiver::ClientRequest(void *p, char *, char *, char *name, bool *admit) {
    auto *r = static_cast<Receiver *>(p);
    std::lock_guard<std::mutex> lock(r->clientMutex);
    r->client = name ? name : "Mac"; *admit = true;
}
}
