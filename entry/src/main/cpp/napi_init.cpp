#include "receiver.h"
#include "state_events.h"
#include "view_gesture.h"
#include "ui_events.h"
#include <napi/native_api.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <memory>
#include <unordered_set>
#include <hilog/log.h>

using qiscreen::Receiver;
namespace {
std::mutex eventMutex;
qiscreen::UiEvents pendingEvents;
napi_threadsafe_function eventFunction = nullptr;
napi_env eventEnv = nullptr;
std::unordered_set<napi_env> cleanupEnvs;
std::mutex touchMutex;
qiscreen::ViewGesture view;
int64_t viewSession = 0;
std::atomic<uint64_t> touchVersion{0};

void CleanupEvents(void *data) {
    std::lock_guard<std::mutex> lock(eventMutex);
    auto env = static_cast<napi_env>(data);
    if (eventEnv == env && eventFunction) {
        auto old = eventFunction; eventFunction = nullptr; eventEnv = nullptr;
        napi_release_threadsafe_function(old, napi_tsfn_abort);
    }
    cleanupEnvs.erase(env);
}
napi_value SetStateCallback(napi_env env, napi_callback_info info) {
    size_t argc = 1; napi_value arg; napi_get_cb_info(env, info, &argc, &arg, nullptr, nullptr);
    napi_valuetype type = napi_undefined;
    if (argc) napi_typeof(env, arg, &type);
    if (type != napi_function && type != napi_null && type != napi_undefined) {
        napi_throw_type_error(env, nullptr, "状态回调必须为函数或 null"); return nullptr;
    }
    napi_threadsafe_function replacement = nullptr;
    if (type == napi_function) {
        napi_value label; napi_create_string_utf8(env, "QiScreenState", NAPI_AUTO_LENGTH, &label);
        if (napi_create_threadsafe_function(env, arg, nullptr, label, 1, 1, nullptr, nullptr, nullptr,
            [](napi_env env, napi_value callback, void *, void *) {
                if (!env || !callback) return;
                napi_value receiver, result, viewOnly; napi_get_undefined(env, &receiver);
                napi_get_boolean(env,pendingEvents.TakeViewOnly(),&viewOnly);
                napi_call_function(env, receiver, callback, 1, &viewOnly, &result);
            }, &replacement) != napi_ok) {
            napi_throw_error(env, nullptr, "无法注册状态通知"); return nullptr;
        }
        napi_unref_threadsafe_function(env, replacement);
    }
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (replacement && !cleanupEnvs.count(env)) {
            if (napi_add_env_cleanup_hook(env, CleanupEvents, env) != napi_ok) {
                napi_release_threadsafe_function(replacement, napi_tsfn_abort);
                napi_throw_error(env, nullptr, "无法注册状态通知清理"); return nullptr;
            }
            cleanupEnvs.insert(env);
        }
        auto old = eventFunction;
        eventFunction = replacement; eventEnv = replacement ? env : nullptr;
        if (old) napi_release_threadsafe_function(old, napi_tsfn_abort);
    }
    qiscreen::NotifyUi();
    napi_value result; napi_get_undefined(env, &result); return result;
}
struct Work {
    napi_async_work work{}; napi_deferred deferred{};
    bool start = false, ok = false, highQuality = false;
    uint64_t command = 0;
    std::string name, filesDir;
    int width = 1920, height = 1080, fps = 60;
};
bool GetString(napi_env env, napi_value value, std::string &out) {
    size_t size = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &size) != napi_ok || size > 4096) return false;
    std::vector<char> buffer(size + 1);
    if (napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &size) != napi_ok) return false;
    out.assign(buffer.data(), size); return true;
}
napi_value Enqueue(napi_env env, std::unique_ptr<Work> work) {
    // Ticket is assigned in invocation order on the ArkTS thread, not worker execution order.
    work->command = Receiver::Instance().NewCommand();
    napi_value promise, label;
    napi_create_promise(env, &work->deferred, &promise);
    napi_create_string_utf8(env, "QiScreenLifecycle", NAPI_AUTO_LENGTH, &label);
    napi_status status = napi_create_async_work(env, nullptr, label,
        [](napi_env, void *data) {
            auto *w = static_cast<Work *>(data);
            if (w->start) w->ok = Receiver::Instance().Start(w->name, w->filesDir, w->width, w->height, w->fps, w->command, w->highQuality);
            else w->ok = Receiver::Instance().Stop(w->command);
        },
        [](napi_env env, napi_status, void *data) {
            std::unique_ptr<Work> w(static_cast<Work *>(data));
            napi_value result; napi_get_boolean(env, w->ok, &result);
            napi_resolve_deferred(env, w->deferred, result); napi_delete_async_work(env, w->work);
        }, work.get(), &work->work);
    if (status != napi_ok || napi_queue_async_work(env, work->work) != napi_ok) {
        napi_value error; napi_create_string_utf8(env, "无法启动工作线程", NAPI_AUTO_LENGTH, &error);
        napi_reject_deferred(env, work->deferred, error);
        if (work->work) napi_delete_async_work(env, work->work);
        return promise;
    }
    work.release(); return promise;
}
napi_value Start(napi_env env, napi_callback_info info) {
    size_t argc = 6; napi_value args[6]; napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    auto work = std::make_unique<Work>(); work->start = true;
    if (argc != 6 || !GetString(env, args[0], work->name) || !GetString(env, args[1], work->filesDir) ||
        napi_get_value_int32(env, args[2], &work->width) != napi_ok ||
        napi_get_value_int32(env, args[3], &work->height) != napi_ok || napi_get_value_int32(env, args[4], &work->fps) != napi_ok ||
        napi_get_value_bool(env, args[5], &work->highQuality) != napi_ok) {
        napi_throw_type_error(env, nullptr, "接收参数无效"); return nullptr;
    }
    return Enqueue(env, std::move(work));
}
napi_value Stop(napi_env env, napi_callback_info) { return Enqueue(env, std::make_unique<Work>()); }
napi_value SetLowLatency(napi_env env, napi_callback_info info) {
    size_t argc = 1; napi_value arg; bool enabled = true;
    napi_get_cb_info(env, info, &argc, &arg, nullptr, nullptr);
    if (argc != 1 || napi_get_value_bool(env, arg, &enabled) != napi_ok) {
        napi_throw_type_error(env, nullptr, "显示策略参数无效"); return nullptr;
    }
    auto &stats = Receiver::Instance().stats;
    if (stats.lowLatency.exchange(enabled) != enabled) stats.ResetLatencyWindows();
    qiscreen::NotifyUi();
    napi_value result; napi_get_undefined(env, &result); return result;
}
napi_value SetViewport(napi_env env, napi_callback_info info) {
    size_t argc = 4; napi_value args[4]; double v[4];
    napi_get_cb_info(env,info,&argc,args,nullptr,nullptr);
    if (argc != 4) { napi_throw_type_error(env,nullptr,"画面区域参数无效"); return nullptr; }
    for (size_t i=0;i<4;++i) if(napi_get_value_double(env,args[i],&v[i]) != napi_ok || !std::isfinite(v[i])) {
        napi_throw_type_error(env,nullptr,"画面区域参数无效"); return nullptr;
    }
    { std::lock_guard<std::mutex> lock(touchMutex); view.Viewport(v[0],v[1],v[2],v[3]); }
    qiscreen::NotifyUi(true); napi_value result; napi_get_undefined(env,&result); return result;
}
napi_value SetZoom(napi_env env, napi_callback_info info) {
    size_t argc=1; napi_value arg; double value;
    napi_get_cb_info(env,info,&argc,&arg,nullptr,nullptr);
    if(argc!=1 || napi_get_value_double(env,arg,&value)!=napi_ok || !std::isfinite(value)) {
        napi_throw_type_error(env,nullptr,"缩放比例无效"); return nullptr;
    }
    { std::lock_guard<std::mutex> lock(touchMutex); view.Cancel(); view.SetScale(value); }
    qiscreen::NotifyUi(true); napi_value result; napi_get_undefined(env,&result); return result;
}
napi_value FillView(napi_env env, napi_callback_info) {
    { std::lock_guard<std::mutex> lock(touchMutex); view.Fill(); }
    qiscreen::NotifyUi(true); napi_value result; napi_get_undefined(env,&result); return result;
}
void Number(napi_env e, napi_value o, const char *k, double x) { napi_value v; napi_create_double(e,x,&v); napi_set_named_property(e,o,k,v); }
void Bool(napi_env e, napi_value o, const char *k, bool x) { napi_value v; napi_get_boolean(e,x,&v); napi_set_named_property(e,o,k,v); }
void String(napi_env e, napi_value o, const char *k, const std::string &x) { napi_value v; napi_create_string_utf8(e,x.c_str(),x.size(),&v); napi_set_named_property(e,o,k,v); }
void WriteView(napi_env env,napi_value result) {
    auto &r=Receiver::Instance();const int64_t first=r.stats.firstFrameAtUs.load();
    std::lock_guard<std::mutex> lock(touchMutex);
    if(first!=viewSession){view.Reset();view.Cancel();viewSession=first;}
    view.Content(r.stats.width,r.stats.height);
    Number(env,result,"touchVersion",touchVersion.load());
    Number(env,result,"viewScale",view.scale);Number(env,result,"viewPanX",view.panX);Number(env,result,"viewPanY",view.panY);
}
napi_value ViewStatus(napi_env env,napi_callback_info) {
    napi_value result;napi_create_object(env,&result);WriteView(env,result);return result;
}
napi_value Status(napi_env env, napi_callback_info) {
    auto &r = Receiver::Instance(); napi_value result; napi_create_object(env,&result);
    Bool(env,result,"ready",r.surfaceReady); Bool(env,result,"listening",r.listening);
    Bool(env,result,"streaming",r.streaming && r.stats.firstFrameAtUs.load() > 0);
    Number(env,result,"frames",r.stats.frames); Number(env,result,"dropped",r.stats.dropped); Number(env,result,"audioFrames",r.stats.audioFrames);
    Number(env,result,"width",r.stats.width); Number(env,result,"height",r.stats.height); Number(env,result,"port",r.port);
    Number(env,result,"serviceReadyMs",r.stats.serviceReadyMs); Number(env,result,"decoderSetupMs",r.stats.decoderSetupMs);
    Number(env,result,"firstFrameMs",r.stats.firstFrameMs);
    const int64_t first = r.stats.firstFrameAtUs.load();
    Number(env,result,"stateNotifyMs",first ? (qiscreen::MonotonicUs() - first) / 1000 : -1);
    Bool(env,result,"lowLatency",r.stats.lowLatency);
    String(env,result,"videoCodec",r.stats.hevc ? "H.265" : "H.264");
    Bool(env,result,"hardwareVideo",r.stats.hardwareVideo);
    Number(env,result,"requestedWidth",r.stats.requestedWidth);
    Number(env,result,"requestedHeight",r.stats.requestedHeight);
    Number(env,result,"receivedFrames",r.stats.received);
    Number(env,result,"receivedBytes",r.stats.receivedBytes);
    WriteView(env,result);
    const auto metrics = r.stats.Diagnostics();
    Number(env,result,"queueP95Ms",metrics.queue.p95Ms); Number(env,result,"decodeP95Ms",metrics.decode.p95Ms);
    Number(env,result,"sourceAgeP95Ms",metrics.sourceAge.p95Ms);
    Number(env,result,"queuedFrames",r.stats.queuedFrames.load());
    Number(env,result,"ntpRttMs", r.stats.ntpRttUs.load() < 0 ? -1.0 : r.stats.ntpRttUs.load() / 1000.0);
    Number(env,result,"decodedFrames",r.stats.decoded); Number(env,result,"skippedFrames",r.stats.skippedOutputs);
    const int64_t now = qiscreen::MonotonicUs();
    int64_t last = r.stats.lastPerfLogUs.load();
    if (r.streaming && r.stats.decoded.load() && now - last >= 1000000 && r.stats.lastPerfLogUs.compare_exchange_strong(last,now)) {
        OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreenPerf",
            "mode=%{public}d received=%{public}llu decoded=%{public}llu shown=%{public}llu skipped=%{public}llu dropped=%{public}llu matched=%{public}llu bytes=%{public}llu queue=%{public}d peak=%{public}d queue_p95_ms=%{public}.3f decode_p95_ms=%{public}.3f source_age_p95_ms=%{public}.3f ntp_rtt_ms=%{public}.3f",
            r.stats.lowLatency.load() ? 1 : 0, static_cast<unsigned long long>(r.stats.received.load()),
            static_cast<unsigned long long>(r.stats.decoded.load()), static_cast<unsigned long long>(r.stats.frames.load()),
            static_cast<unsigned long long>(r.stats.skippedOutputs.load()), static_cast<unsigned long long>(r.stats.dropped.load()),
            static_cast<unsigned long long>(r.stats.timingMatches.load()), static_cast<unsigned long long>(r.stats.receivedBytes.load()), r.stats.queuedFrames.load(), r.stats.queuePeak.load(),
            metrics.queue.p95Ms, metrics.decode.p95Ms, metrics.sourceAge.p95Ms,
            r.stats.ntpRttUs.load() < 0 ? -1.0 : r.stats.ntpRttUs.load() / 1000.0);
    }
    { std::lock_guard<std::mutex> lock(r.stats.errorMutex); String(env,result,"error",r.stats.error); }
    { std::lock_guard<std::mutex> lock(r.clientMutex); String(env,result,"client",r.client); }
    return result;
}
OH_NativeXComponent_Callback surfaceCallbacks{
    [](OH_NativeXComponent *, void *window) { Receiver::Instance().SetSurface(static_cast<OHNativeWindow *>(window)); },
    [](OH_NativeXComponent *, void *) {},
    [](OH_NativeXComponent *, void *) {
        { std::lock_guard<std::mutex> lock(touchMutex); view.Cancel(); view.Reset(); }
        Receiver::Instance().SetSurface(nullptr);
    },
    [](OH_NativeXComponent *component, void *window) {
        OH_NativeXComponent_TouchEvent event{};
        if (OH_NativeXComponent_GetTouchEvent(component,window,&event) != 0) return;
        if (!Receiver::Instance().streaming) return;
        bool tap = false;
        {
            std::lock_guard<std::mutex> lock(touchMutex);
            qiscreen::ViewPoint points[2]; size_t count = 0;
            for (uint32_t i=0;i<event.numPoints && i<OH_MAX_TOUCH_POINTS_NUMBER && count<2;++i) {
                const auto &p = event.touchPoints[i];
                // XComponent sets isPressed only on DOWN (also for every point in the event).
                // On MOVE all listed points remain down; on UP only the changed id leaves.
                if (!(event.type == OH_NATIVEXCOMPONENT_UP && p.id == event.id))
                    points[count++] = {p.id,p.screenX,p.screenY};
            }
            const qiscreen::ViewPoint changed{event.id,event.screenX,event.screenY};
            tap = view.Touch(points,count,changed,qiscreen::MonotonicUs(),event.type == OH_NATIVEXCOMPONENT_CANCEL);
            if (event.type == OH_NATIVEXCOMPONENT_UP && count == 0)
                OH_LOG_Print(LOG_APP, LOG_INFO, 0xD004, "QiScreenView",
                    "zoom=%{public}.3f pan=%{public}.1f,%{public}.1f",view.scale,view.panX,view.panY);
        }
        if (tap) ++touchVersion;
        qiscreen::NotifyUi(true);
    }
};
napi_value Init(napi_env env, napi_value exports) {
    napi_value componentObject; bool hasComponent = false;
    napi_has_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &hasComponent);
    if (hasComponent && napi_get_named_property(env,exports,OH_NATIVE_XCOMPONENT_OBJ,&componentObject) == napi_ok) {
        OH_NativeXComponent *component = nullptr;
        if (napi_unwrap(env,componentObject,reinterpret_cast<void **>(&component)) == napi_ok && component)
            OH_NativeXComponent_RegisterCallback(component,&surfaceCallbacks);
    }
    napi_property_descriptor properties[] = {
        {"start",nullptr,Start,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"stop",nullptr,Stop,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"viewStatus",nullptr,ViewStatus,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"status",nullptr,Status,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"setStateCallback",nullptr,SetStateCallback,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"setLowLatency",nullptr,SetLowLatency,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"setViewport",nullptr,SetViewport,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"setZoom",nullptr,SetZoom,nullptr,nullptr,nullptr,napi_default,nullptr},
        {"fillView",nullptr,FillView,nullptr,nullptr,nullptr,napi_default,nullptr}
    };
    napi_define_properties(env,exports,sizeof(properties)/sizeof(properties[0]),properties); return exports;
}
napi_module module{1,0,nullptr,Init,"qiscreen",nullptr,{0}};
}
namespace qiscreen {
void NotifyUi(bool viewOnly) {
    std::lock_guard<std::mutex> lock(eventMutex);
    pendingEvents.Add(viewOnly);
    if (eventFunction) napi_call_threadsafe_function(eventFunction, nullptr, napi_tsfn_nonblocking);
}
}
extern "C" __attribute__((constructor)) void RegisterQiScreen() { napi_module_register(&module); }
