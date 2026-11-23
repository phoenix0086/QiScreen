#include "media.h"
#include "state_events.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <hilog/log.h>
#include <multimedia/player_framework/native_avcodec_videodecoder.h>
#include <multimedia/player_framework/native_avcodec_audiocodec.h>
#include <multimedia/player_framework/native_avformat.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <ohaudio/native_audiostreambuilder.h>

namespace qiscreen {
void MediaStats::ResetLatencyWindows() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    queueLatency.Reset(); decodeLatency.Reset(); sourceAge.Reset();
    queuePeak = queuedFrames.load(); lastPerfLogUs = 0;
}
void MediaStats::ResetDiagnostics() {
    ResetLatencyWindows(); received = 0; decoded = 0; skippedOutputs = 0; timingMatches = 0;
    queuedFrames = 0; queuePeak = 0; ntpRttUs = -1; receivedBytes = 0;
}
PipelineDiagnostics MediaStats::Diagnostics() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    return {queueLatency.Snapshot(), decodeLatency.Snapshot(), sourceAge.Snapshot()};
}
void MediaStats::Error(const std::string &message) {
    { std::lock_guard<std::mutex> lock(errorMutex); error = message; }
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD004, "QiScreen", "%{public}s", message.c_str());
    NotifyUi();
}

bool MediaDecoder::SupportsHevc(int width, int height, int fps) {
    auto *cap = OH_AVCodec_GetCapabilityByCategory(OH_AVCODEC_MIMETYPE_VIDEO_HEVC, false, HARDWARE);
    return cap && OH_AVCapability_AreVideoSizeAndFrameRateSupported(cap,width,height,fps);
}
bool MediaDecoder::StartVideo(OHNativeWindow *window, int width, int height, bool hevc) {
    std::lock_guard<std::mutex> lock(lifecycle_);
    StopLocked();
    const int64_t setupStarted = MonotonicUs();
    if (!window || width < 16 || height < 16 || width > 4096 || height > 4096) return false;
    const char *mime = hevc ? OH_AVCODEC_MIMETYPE_VIDEO_HEVC : OH_AVCODEC_MIMETYPE_VIDEO_AVC;
    auto *cap = OH_AVCodec_GetCapabilityByCategory(mime, false, HARDWARE);
    if (!cap || !OH_AVCapability_IsVideoSizeSupported(cap, width, height)) {
        stats_.Error("设备不支持此视频格式的硬件解码，请选择 1080p 后重连"); return false;
    }
    const char *name = OH_AVCapability_GetName(cap);
    codec_ = name ? OH_VideoDecoder_CreateByName(name) : nullptr;
    stats_.hardwareVideo = true; stats_.hevc = hevc;
    OH_AVFormat *format = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_WIDTH, width);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_HEIGHT, height);
    OH_AVFormat_SetDoubleValue(format, OH_MD_KEY_FRAME_RATE, static_cast<double>(stats_.targetFps.load()));
    if (OH_AVCapability_IsFeatureSupported(cap, VIDEO_LOW_LATENCY))
        OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENABLE_LOW_LATENCY, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_MAX_INPUT_SIZE, 4 * 1024 * 1024);
    OH_NativeWindow_NativeWindowSetScalingModeV2(window, OH_SCALING_MODE_SCALE_FIT_V2);
    bool ok = StartCodec(format, window);
    OH_AVFormat_Destroy(format);
    if (ok) {
        stats_.width = width; stats_.height = height;
        stats_.decoderSetupMs = (MonotonicUs() - setupStarted) / 1000;
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "Video format %{public}s %{public}dx%{public}d; hardware=%{public}s",
            hevc ? "HEVC" : "AVC", width, height, name);
    }
    return ok;
}

bool MediaDecoder::StartAudio(uint8_t ct) {
    std::lock_guard<std::mutex> lock(lifecycle_);
    StopLocked();
    if (ct != 2 && ct != 4 && ct != 8) {
        stats_.Error("不支持的音频格式：" + std::to_string(ct));
        return false;
    }
    codec_ = OH_AudioCodec_CreateByMime(ct == 2 ? OH_AVCODEC_MIMETYPE_AUDIO_ALAC : OH_AVCODEC_MIMETYPE_AUDIO_AAC, false);
    OH_AVFormat *format = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_AUD_CHANNEL_COUNT, 2);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_AUD_SAMPLE_RATE, 44100);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_AUDIO_SAMPLE_FORMAT, SAMPLE_S16LE);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_AAC_IS_ADTS, 0);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_MAX_INPUT_SIZE, 16384);
    // AudioSpecificConfig / ALAC cookie as specified by the AirPlay stream format.
    const uint8_t eld[] = {0xf8, 0xe8, 0x50, 0x00};
    const uint8_t lc[] = {0x12, 0x10};
    const uint8_t alac[] = {0,0,0,0x24,0x61,0x6c,0x61,0x63,0,0,0,0,0,0,1,0x60,
        0,0x10,0x28,0x0a,0x0e,2,0,0xff,0,0,0,0,0,0,0,0,0,0,0xac,0x44};
    OH_AVFormat_SetBuffer(format, OH_MD_KEY_CODEC_CONFIG,
        ct == 2 ? alac : (ct == 8 ? eld : lc), ct == 2 ? sizeof(alac) : (ct == 8 ? sizeof(eld) : sizeof(lc)));
    bool ok = CreateRenderer() && StartCodec(format, nullptr);
    OH_AVFormat_Destroy(format);
    if (!ok) StopLocked();
    return ok;
}

bool MediaDecoder::StartCodec(OH_AVFormat *format, OHNativeWindow *window) {
    if (!codec_) { stats_.Error(video_ ? "无法创建视频解码器" : "无法创建音频解码器"); return false; }
    OH_AVCodecCallback callbacks{OnError, OnFormat, OnInput, OnOutput};
    OH_AVErrCode rc;
    if (video_) {
        rc = OH_VideoDecoder_RegisterCallback(codec_, callbacks, this);
        if (rc == AV_ERR_OK) rc = OH_VideoDecoder_Configure(codec_, format);
        if (rc == AV_ERR_OK) rc = OH_VideoDecoder_SetSurface(codec_, window);
        if (rc == AV_ERR_OK) rc = OH_VideoDecoder_Prepare(codec_);
    } else {
        rc = OH_AudioCodec_RegisterCallback(codec_, callbacks, this);
        if (rc == AV_ERR_OK) rc = OH_AudioCodec_Configure(codec_, format);
        if (rc == AV_ERR_OK) rc = OH_AudioCodec_Prepare(codec_);
    }
    if (rc != AV_ERR_OK) {
        stats_.Error(std::string(video_ ? "视频" : "音频") + "解码器配置失败：" + std::to_string(rc));
        StopLocked(); return false;
    }
    queue_.Start(video_ ? 8 : 24, video_ ? 8 * 1024 * 1024 : 256 * 1024);
    running_ = true;
    feeder_ = std::thread(&MediaDecoder::Feed, this);
    rc = video_ ? OH_VideoDecoder_Start(codec_) : OH_AudioCodec_Start(codec_);
    if (rc != AV_ERR_OK) {
        stats_.Error("解码器启动失败：" + std::to_string(rc)); StopLocked(); return false;
    }
    OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "%{public}s decoder started", video_ ? "video" : "audio");
    return true;
}

void MediaDecoder::Submit(const uint8_t *data, size_t size, int64_t pts, int64_t captureUs) {
    if (!data || size == 0 || !running_) return;
    if (size > 4 * 1024 * 1024) {
        Interrupt(); stats_.dropped++;
        stats_.Error("媒体帧超出接收容量，请重新连接"); return;
    }
    const int64_t receivedUs = MonotonicUs();
    if (video_) { stats_.received++; stats_.receivedBytes += size; latestCaptureUs_ = captureUs; }
    // Reliable, ordered TCP input: keep reference frames and let the producer wait for space.
    if (!queue_.Push({std::vector<uint8_t>(data, data + size), pts, receivedUs, captureUs})) return;
    if (video_) {
        const int depth = static_cast<int>(queue_.Depth()); stats_.queuedFrames = depth;
        int peak = stats_.queuePeak.load();
        while (peak < depth && !stats_.queuePeak.compare_exchange_weak(peak, depth)) {}
    }
}

void MediaDecoder::Feed() {
    for (;;) {
        Input input{}; Packet packet;
        if (!queue_.Take(input, packet)) return;
        if (video_) stats_.queuedFrames = static_cast<int>(queue_.Depth());
        auto *dst = OH_AVBuffer_GetAddr(input.buffer);
        int capacity = OH_AVBuffer_GetCapacity(input.buffer);
        if (!dst || capacity < 0 || packet.bytes.size() > static_cast<size_t>(capacity)) {
            stats_.dropped++;
            stats_.Error("媒体帧超过解码缓冲容量，请降低分辨率后重连");
            // Stop this stream instead of feeding dependent frames after a lost reference.
            Interrupt(); return;
        }
        std::memcpy(dst, packet.bytes.data(), packet.bytes.size());
        OH_AVCodecBufferAttr attr{};
        attr.pts = packet.pts; attr.size = static_cast<int32_t>(packet.bytes.size());
        attr.offset = 0; attr.flags = AVCODEC_BUFFER_FLAGS_NONE;
        OH_AVBuffer_SetBufferAttr(input.buffer, &attr);
        const int64_t submittedUs = MonotonicUs();
        if (video_) {
            { std::lock_guard<std::mutex> lock(stats_.metricsMutex);
              stats_.queueLatency.Add(submittedUs - packet.receivedUs); }
            { std::lock_guard<std::mutex> lock(flightMutex_);
              if (flights_.size() >= 128) flights_.pop_front();
              flights_.push_back({packet.pts, packet.receivedUs, submittedUs, packet.captureUs}); }
        }
        OH_AVErrCode rc = video_ ? OH_VideoDecoder_PushInputBuffer(codec_, input.index)
                                : OH_AudioCodec_PushInputBuffer(codec_, input.index);
        if (rc != AV_ERR_OK) {
            if (video_) {
                std::lock_guard<std::mutex> lock(flightMutex_);
                auto it = std::find_if(flights_.begin(), flights_.end(), [&](const Flight &f) {
                    return f.pts == packet.pts && f.submittedUs == submittedUs;
                });
                if (it != flights_.end()) flights_.erase(it);
            }
            if (running_) stats_.Error("送入解码器失败：" + std::to_string(rc));
            Interrupt(); return;
        }
    }
}

void MediaDecoder::Interrupt() { running_ = false; queue_.Stop(); }
void MediaDecoder::Stop() { std::lock_guard<std::mutex> lock(lifecycle_); StopLocked(); }
void MediaDecoder::StopLocked() {
    Interrupt();
    if (feeder_.joinable()) feeder_.join();
    if (codec_) {
        if (video_) { OH_VideoDecoder_Stop(codec_); OH_VideoDecoder_Destroy(codec_); }
        else { OH_AudioCodec_Stop(codec_); OH_AudioCodec_Destroy(codec_); }
        codec_ = nullptr;
    }
    if (renderer_) { OH_AudioRenderer_Stop(renderer_); OH_AudioRenderer_Release(renderer_); renderer_ = nullptr; }
    queue_.Clear();
    { std::lock_guard<std::mutex> lock(pcmMutex_); pcm_.Clear(); }
    { std::lock_guard<std::mutex> lock(flightMutex_); flights_.clear(); }
    latestCaptureUs_ = 0; lastPresentedUs_ = 0;
    if (video_) stats_.queuedFrames = 0;
}

void MediaDecoder::OnError(OH_AVCodec *, int32_t code, void *user) {
    auto *self = static_cast<MediaDecoder *>(user);
    self->Interrupt();
    self->stats_.Error(std::string(self->video_ ? "视频" : "音频") + "解码错误：" + std::to_string(code));
}
void MediaDecoder::OnFormat(OH_AVCodec *, OH_AVFormat *format, void *user) {
    auto *self = static_cast<MediaDecoder *>(user);
    if (self->video_) {
        int w = 0, h = 0;
        OH_AVFormat_GetIntValue(format, OH_MD_KEY_WIDTH, &w);
        OH_AVFormat_GetIntValue(format, OH_MD_KEY_HEIGHT, &h);
        if (w > 0 && h > 0) { self->stats_.width = w; self->stats_.height = h; }
    }
}
void MediaDecoder::OnInput(OH_AVCodec *, uint32_t index, OH_AVBuffer *buffer, void *user) {
    auto *self = static_cast<MediaDecoder *>(user);
    self->queue_.AddInput({index, buffer});
}
void MediaDecoder::OnOutput(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *buffer, void *user) {
    auto *self = static_cast<MediaDecoder *>(user);
    if (self->video_) {
        const int64_t now = MonotonicUs();
        OH_AVCodecBufferAttr attr{}; Flight flight{}; bool matched = false;
        if (OH_AVBuffer_GetBufferAttr(buffer, &attr) == AV_ERR_OK) {
            std::lock_guard<std::mutex> lock(self->flightMutex_);
            auto it = std::find_if(self->flights_.begin(), self->flights_.end(), [&](const Flight &f) { return f.pts == attr.pts; });
            if (it != self->flights_.end()) { flight = *it; self->flights_.erase(it); matched = true; }
        }
        if (self->running_) self->stats_.decoded++;
        if (matched) {
            self->stats_.timingMatches++;
            std::lock_guard<std::mutex> lock(self->stats_.metricsMutex);
            self->stats_.decodeLatency.Add(now - flight.submittedUs);
        }
        // Keep every compressed reference frame in the decoder. Only skip obsolete presentation.
        if (self->running_ && matched && SkipObsoleteOutput(self->stats_.lowLatency,
            flight.captureUs, self->latestCaptureUs_, flight.receivedUs, now, self->lastPresentedUs_)) {
            if (OH_VideoDecoder_FreeOutputBuffer(codec, index) == AV_ERR_OK) self->stats_.skippedOutputs++;
            return;
        }
        if (self->running_ && OH_VideoDecoder_RenderOutputBuffer(codec, index) == AV_ERR_OK) {
            self->lastPresentedUs_ = now;
            if (++self->stats_.frames == 1) {
                const int64_t now = MonotonicUs();
                self->stats_.firstFrameAtUs = now;
                const int64_t control = self->stats_.controlConnectedUs.load();
                self->stats_.firstFrameMs = control ? (now - control) / 1000 : -1;
                OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "First video frame rendered; control-to-frame %{public}lld ms; decoder setup %{public}lld ms",
                    static_cast<long long>(self->stats_.firstFrameMs.load()), static_cast<long long>(self->stats_.decoderSetupMs.load()));
                NotifyUi();
            }
        }
        else OH_VideoDecoder_FreeOutputBuffer(codec, index);
        return;
    }
    OH_AVCodecBufferAttr attr{};
    OH_AVBuffer_GetBufferAttr(buffer, &attr);
    uint8_t *data = OH_AVBuffer_GetAddr(buffer);
    const int cap = OH_AVBuffer_GetCapacity(buffer);
    if (self->running_ && data && attr.offset >= 0 && attr.size > 0 && attr.offset <= cap && attr.size <= cap - attr.offset) {
        std::lock_guard<std::mutex> lock(self->pcmMutex_);
        if (self->pcm_.Write(data + attr.offset, static_cast<size_t>(attr.size))) {
            if (++self->stats_.audioFrames == 1) OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreen", "First audio PCM buffer decoded");
        }
    }
    OH_AudioCodec_FreeOutputBuffer(codec, index);
}

bool MediaDecoder::CreateRenderer() {
    OH_AudioStreamBuilder *builder = nullptr;
    if (OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER) != AUDIOSTREAM_SUCCESS) return false;
    OH_AudioStreamBuilder_SetSamplingRate(builder, 44100);
    OH_AudioStreamBuilder_SetChannelCount(builder, 2);
    OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_S16LE);
    OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
    OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);
    OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_MOVIE);
    OH_AudioRenderer_Callbacks callbacks{};
    callbacks.OH_AudioRenderer_OnWriteData = WriteAudio;
    OH_AudioStreamBuilder_SetRendererCallback(builder, callbacks, this);
    auto rc = OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer_);
    OH_AudioStreamBuilder_Destroy(builder);
    if (rc != AUDIOSTREAM_SUCCESS || !renderer_) { stats_.Error("音频输出初始化失败"); return false; }
    OH_AudioRenderer_SetVolume(renderer_, volume_);
    return OH_AudioRenderer_Start(renderer_) == AUDIOSTREAM_SUCCESS;
}
int32_t MediaDecoder::WriteAudio(OH_AudioRenderer *, void *user, void *buffer, int32_t length) {
    auto *self = static_cast<MediaDecoder *>(user);
    if (!buffer || length <= 0) return 0;
    std::memset(buffer, 0, length);
    std::lock_guard<std::mutex> lock(self->pcmMutex_);
    auto *out = static_cast<uint8_t *>(buffer);
    self->pcm_.Read(out, static_cast<size_t>(length));
    return 0;
}
void MediaDecoder::SetVolume(float volume) {
    std::lock_guard<std::mutex> lock(lifecycle_);
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (renderer_) OH_AudioRenderer_SetVolume(renderer_, volume_);
}
}
