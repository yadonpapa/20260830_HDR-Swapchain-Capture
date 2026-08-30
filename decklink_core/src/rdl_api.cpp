// 流用元: 20260611_rawdecklink-signal-player/core/src/rdl_api.cpp
// 改変点(Phase 7 Task 2): rdl_start_playback_memory / rdl_update_frame を追加
//   (既存関数・ハンドル管理は不変)。
// core/src/rdl_api.cpp — C ABIグルー(ハンドル管理)
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <new>

#include "platform_compat.h"
#include "playback.h"
#include "capture.h"
#include "rdl_api.h"

IDeckLink* rdl_internal_device(int index);  // device_enum.cpp

namespace {
void copy_err(char* buf, int len, const std::string& s) {
    if (!buf || len <= 0) return;
    size_t n = s.size() < (size_t)len - 1 ? s.size() : (size_t)len - 1;
    memcpy(buf, s.c_str(), n);
    buf[n] = '\0';
}

// IDeckLinkProfile::SetActive は非同期。SDK の IDL に明記:
//   "Activation is not complete until IDeckLinkProfileCallback::ProfileActivated is called"
// MTA(COINIT_MULTITHREADED)下では ProfileActivated は任意のRPCスレッドで配送されるため、
// メッセージポンプではなく condition_variable で完了を待つ(OS非依存)。これを待たずに
// デバイスを再列挙すると旧構成が見える(揮発に見える)現象が起きる。
class ProfileWaitCallback : public IDeckLinkProfileCallback {
public:
    explicit ProfileWaitCallback(BMDProfileID target) : ref_(1), target_(target) {}

    // 目的 profile の ProfileActivated 到着を待つ。true=到着 / false=タイムアウト
    bool wait_activated(int timeout_ms) {
        std::unique_lock<std::mutex> lk(mu_);
        return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [this] { return done_; });
    }

    // 切替前。streamsWillBeForcedToStop は本用途では参照しない(再生は呼び出し側で停止済み)。
    HRESULT STDMETHODCALLTYPE ProfileChanging(IDeckLinkProfile*, dlbool_t) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProfileActivated(IDeckLinkProfile* activated) override {
        // 目的の profile が有効化されたときのみシグナル(他要因の活性化通知を弾く)。
        if (activated) {
            IDeckLinkProfileAttributes* a = nullptr;
            if (activated->QueryInterface(IID_IDeckLinkProfileAttributes,
                                          (void**)&a) == S_OK && a) {
                int64_t pid = 0;
                if (a->GetInt(BMDDeckLinkProfileID, &pid) == S_OK &&
                    (BMDProfileID)pid == target_) {
                    std::lock_guard<std::mutex> lk(mu_);
                    done_ = true;
                    cv_.notify_all();
                }
                a->Release();
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (rdl_iid_equal(riid, RDL_IID_IUNKNOWN) ||
            rdl_iid_equal(riid, IID_IDeckLinkProfileCallback)) {
            *ppv = static_cast<IDeckLinkProfileCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref_;
        if (r == 0) delete this;
        return r;
    }

private:
    std::atomic<ULONG> ref_;
    BMDProfileID target_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_ = false;
};
}  // namespace

extern "C" {

RDL_EXPORT int rdl_sizeof(int struct_id) {
    switch (struct_id) {
        case 1: return (int)sizeof(RdlDeviceInfo);
        case 2: return (int)sizeof(RdlModeInfo);
        case 3: return (int)sizeof(RdlPlaybackConfig);
        case 4: return (int)sizeof(RdlStatus);
        case 5: return (int)sizeof(RdlFrameCrc);
        case 6: return (int)sizeof(RdlSdiSettings);
        case 7: return (int)sizeof(RdlProfileList);
        case 8: return (int)sizeof(RdlCapturedInfo);
        default: return RDL_E_BADARG;
    }
}

// 方針: C++例外が C ABI(→ctypes)を越えるとUBのため、全エクスポート関数の
// 本体を try/catch で包む(値を返すものは RDL_E_INTERNAL、void は握りつぶし)。

RDL_EXPORT int rdl_get_sdi_settings(int device_index, RdlSdiSettings* out) try {
    if (!out) return RDL_E_BADARG;
    IDeckLink* dev = rdl_internal_device(device_index);
    if (!dev) return RDL_E_BADARG;
    IDeckLinkConfiguration* c = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkConfiguration, (void**)&c) != S_OK || !c)
        return RDL_E_INTERNAL;
    dlbool_t f444 = 0, lvlA = 0;
    int64_t link = 0, mode = 0;
    HRESULT h1 = c->GetFlag(bmdDeckLinkConfig444SDIVideoOutput, &f444);
    HRESULT h2 = c->GetInt(bmdDeckLinkConfigSDIOutputLinkConfiguration, &link);
    HRESULT h3 = c->GetFlag(bmdDeckLinkConfigSMPTELevelAOutput, &lvlA);
    HRESULT h4 = c->GetInt(bmdDeckLinkConfigDefaultVideoOutputMode, &mode);
    c->Release();
    if (h1 != S_OK || h2 != S_OK || h3 != S_OK || h4 != S_OK) return RDL_E_INTERNAL;
    out->sdi_444 = f444 ? 1 : 0;
    out->sdi_link = (int64_t)link;
    out->level_a = lvlA ? 1 : 0;
    out->default_mode = (int64_t)mode;
    return RDL_OK;
} catch (...) {
    return RDL_E_INTERNAL;
}

RDL_EXPORT int rdl_set_sdi_settings(int device_index, const RdlSdiSettings* s) try {
    if (!s) return RDL_E_BADARG;
    IDeckLink* dev = rdl_internal_device(device_index);
    if (!dev) return RDL_E_BADARG;
    IDeckLinkConfiguration* c = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkConfiguration, (void**)&c) != S_OK || !c)
        return RDL_E_INTERNAL;
    HRESULT h1 = c->SetFlag(bmdDeckLinkConfig444SDIVideoOutput, s->sdi_444 != 0);
    HRESULT h2 = c->SetInt(bmdDeckLinkConfigSDIOutputLinkConfiguration,
                           (int64_t)s->sdi_link);
    HRESULT h3 = c->SetFlag(bmdDeckLinkConfigSMPTELevelAOutput, s->level_a != 0);
    HRESULT h4 = c->SetInt(bmdDeckLinkConfigDefaultVideoOutputMode,
                           (int64_t)s->default_mode);
    HRESULT hw = c->WriteConfigurationToPreferences();
    c->Release();
    if (h1 != S_OK || h2 != S_OK || h3 != S_OK || h4 != S_OK || hw != S_OK)
        return RDL_E_INTERNAL;
    return RDL_OK;
} catch (...) {
    return RDL_E_INTERNAL;
}

RDL_EXPORT int rdl_get_connector_profiles(int device_index, RdlProfileList* out) try {
    if (!out) return RDL_E_BADARG;
    IDeckLink* dev = rdl_internal_device(device_index);
    if (!dev) return RDL_E_BADARG;
    IDeckLinkProfileManager* mgr = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkProfileManager, (void**)&mgr) != S_OK || !mgr)
        return RDL_E_INTERNAL;
    IDeckLinkProfileIterator* it = nullptr;
    if (mgr->GetProfiles(&it) != S_OK || !it) { mgr->Release(); return RDL_E_INTERNAL; }
    out->current_profile = 0;
    out->count = 0;
    IDeckLinkProfile* p = nullptr;
    while (it->Next(&p) == S_OK && out->count < 8) {
        IDeckLinkProfileAttributes* a = nullptr;
        if (p->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&a) == S_OK && a) {
            int64_t pid = 0;
            if (a->GetInt(BMDDeckLinkProfileID, &pid) == S_OK) {
                out->profiles[out->count++] = (int64_t)pid;
                dlbool_t active = 0;
                if (p->IsActive(&active) == S_OK && active) out->current_profile = (int64_t)pid;
            }
            a->Release();
        }
        p->Release();
    }
    it->Release();
    mgr->Release();
    return RDL_OK;
} catch (...) {
    return RDL_E_INTERNAL;
}

RDL_EXPORT int rdl_set_connector_profile(int device_index, int64_t profile_id) try {
    IDeckLink* dev = rdl_internal_device(device_index);
    if (!dev) return RDL_E_BADARG;
    IDeckLinkProfileManager* mgr = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkProfileManager, (void**)&mgr) != S_OK || !mgr)
        return RDL_E_INTERNAL;

    // 目的の profile を取得(GetProfile はマネージャ直引きで列挙不要)。
    IDeckLinkProfile* target = nullptr;
    if (mgr->GetProfile((BMDProfileID)profile_id, &target) != S_OK || !target) {
        mgr->Release();
        return RDL_E_BADARG;
    }

    // 既に有効なら何もしない(冪等)。SetActive を呼ぶと再構成で暗転しうるため避ける。
    dlbool_t already = 0;
    if (target->IsActive(&already) == S_OK && already) {
        target->Release();
        mgr->Release();
        return RDL_OK;
    }

    // ProfileActivated を待つためコールバックを登録。
    ProfileWaitCallback* cb = new (std::nothrow)
        ProfileWaitCallback((BMDProfileID)profile_id);
    if (!cb) {
        target->Release();
        mgr->Release();
        return RDL_E_INTERNAL;
    }
    mgr->SetCallback(cb);

    int rc = RDL_E_INTERNAL;
    if (target->SetActive() == S_OK) {
        // MTAではコールバックは別スレッドで配送される。完了を条件変数で待つ。
        // 切替はハード再構成を伴うため余裕を持って10秒待つ。タイムアウト時は内部エラー。
        rc = cb->wait_activated(10000) ? RDL_OK : RDL_E_INTERNAL;
    }

    mgr->SetCallback(nullptr);  // 登録解除(以降の通知でコールバックを使わせない)
    cb->Release();
    target->Release();
    mgr->Release();
    return rc;
} catch (...) {
    return RDL_E_INTERNAL;
}

RDL_EXPORT RdlHandle rdl_open(int device_index) try {
    IDeckLink* dev = rdl_internal_device(device_index);
    if (!dev) return nullptr;
    return new (std::nothrow) PlaybackEngine(dev);
} catch (...) {
    return nullptr;
}

RDL_EXPORT int rdl_start_playback(RdlHandle h, const RdlPlaybackConfig* cfg) {
    if (!h || !cfg) return RDL_E_BADARG;
    try {
        return static_cast<PlaybackEngine*>(h)->start(*cfg);
    } catch (...) {
        return RDL_E_INTERNAL;
    }
}

RDL_EXPORT int rdl_start_playback_memory(RdlHandle h, const RdlPlaybackConfig* cfg,
                                         const void* frame_bytes, int64_t size) {
    if (!h || !cfg || !frame_bytes || size <= 0) return RDL_E_BADARG;
    try {
        return static_cast<PlaybackEngine*>(h)->start_memory(*cfg, frame_bytes, size);
    } catch (...) {
        return RDL_E_INTERNAL;
    }
}

RDL_EXPORT int rdl_update_frame(RdlHandle h, const void* frame_bytes, int64_t size) {
    if (!h) return RDL_E_BADARG;
    try {
        return static_cast<PlaybackEngine*>(h)->update_frame(frame_bytes, size);
    } catch (...) {
        return RDL_E_INTERNAL;
    }
}

RDL_EXPORT int rdl_stop(RdlHandle h) {
    if (!h) return RDL_E_BADARG;
    try {
        return static_cast<PlaybackEngine*>(h)->stop();
    } catch (...) {
        return RDL_E_INTERNAL;
    }
}

RDL_EXPORT void rdl_close(RdlHandle h) try {
    delete static_cast<PlaybackEngine*>(h);   // デストラクタが stop() を呼ぶ
} catch (...) {
    // C ABIを越えて例外を漏らさない(閉じ損ねよりプロセス継続を優先)
}

RDL_EXPORT int rdl_get_status(RdlHandle h, RdlStatus* out) try {
    if (!h || !out) return RDL_E_BADARG;
    static_cast<PlaybackEngine*>(h)->get_status(out);
    return RDL_OK;
} catch (...) {
    return RDL_E_INTERNAL;
}

RDL_EXPORT int rdl_get_frame_crcs(RdlHandle h, RdlFrameCrc* buf, int max_count) try {
    if (!h || !buf || max_count <= 0) return RDL_E_BADARG;
    return static_cast<PlaybackEngine*>(h)->get_frame_crcs(buf, max_count);
} catch (...) {
    return RDL_E_INTERNAL;
}

RDL_EXPORT int rdl_get_last_error(RdlHandle h, char* buf, int len) try {
    if (!h) return RDL_E_BADARG;
    copy_err(buf, len, static_cast<PlaybackEngine*>(h)->last_error());
    return RDL_OK;
} catch (...) {
    return RDL_E_INTERNAL;
}

/* --- HDMI ループバック取り込み(2026-07-26 設計書 §4)。既存 API は不変 --- */

RDL_EXPORT int rdl_input_get_connections(int device_index, int64_t* out_mask) try {
    if (!out_mask) return RDL_E_BADARG;
    IDeckLink* dev = rdl_internal_device(device_index);
    if (!dev) return RDL_E_BADARG;
    *out_mask = 0;
    IDeckLinkInput* in = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkInput, (void**)&in) != S_OK || !in)
        return RDL_OK;   // 入力非対応 → mask 0
    in->Release();
    IDeckLinkProfileAttributes* attr = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&attr) != S_OK
            || !attr)
        return RDL_E_INTERNAL;
    int64_t conns = 0;
    HRESULT hr = attr->GetInt(BMDDeckLinkVideoInputConnections, &conns);
    attr->Release();
    if (hr != S_OK) return RDL_E_INTERNAL;
    *out_mask = conns;
    return RDL_OK;
} catch (...) {
    return RDL_E_INTERNAL;
}

RDL_EXPORT RdlInHandle rdl_input_open(int device_index, int64_t connection) try {
    IDeckLink* dev = rdl_internal_device(device_index);
    if (!dev) return nullptr;
    CaptureEngine* e = new (std::nothrow) CaptureEngine(dev, connection);
    if (e && !e->ok()) { delete e; return nullptr; }
    return e;
} catch (...) {
    return nullptr;
}

RDL_EXPORT int rdl_input_start(RdlInHandle h) {
    if (!h) return RDL_E_BADARG;
    try { return static_cast<CaptureEngine*>(h)->start(); }
    catch (...) { return RDL_E_INTERNAL; }
}

RDL_EXPORT int rdl_input_grab(RdlInHandle h, int skip_frames, int timeout_ms,
                              void* buf, int64_t size, RdlCapturedInfo* out) {
    if (!h) return RDL_E_BADARG;
    try {
        return static_cast<CaptureEngine*>(h)->grab(skip_frames, timeout_ms,
                                                    buf, size, out);
    } catch (...) { return RDL_E_INTERNAL; }
}

RDL_EXPORT int rdl_input_stop(RdlInHandle h) {
    if (!h) return RDL_E_BADARG;
    try { return static_cast<CaptureEngine*>(h)->stop(); }
    catch (...) { return RDL_E_INTERNAL; }
}

RDL_EXPORT void rdl_input_close(RdlInHandle h) try {
    delete static_cast<CaptureEngine*>(h);   // デストラクタが stop する
} catch (...) {
}

RDL_EXPORT int rdl_input_get_last_error(RdlInHandle h, char* buf, int len) try {
    if (!h) return RDL_E_BADARG;
    copy_err(buf, len, static_cast<CaptureEngine*>(h)->last_error());
    return RDL_OK;
} catch (...) {
    return RDL_E_INTERNAL;
}

}  // extern "C"
