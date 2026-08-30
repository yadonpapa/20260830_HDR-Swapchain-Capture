// 流用元: 20260611_rawdecklink-signal-player/core/src/device_enum.cpp （逐語コピー、改変なし）
// core/src/device_enum.cpp — rdl_initialize とデバイス・モード列挙
#include <atomic>
#include <new>
#include <string>
#include <vector>

#include "platform_compat.h"
#include "raw_reader.h"
#include "rdl_api.h"

struct DeviceEntry {
    IDeckLink* device = nullptr;
    RdlDeviceInfo info{};
    std::vector<RdlModeInfo> modes;
};

// g_devices / g_error / g_initialized はPythonメインスレッドからの直列呼び出し前提。
// DeckLinkコールバックスレッドからは触らないこと(Task 12の再生エンジンはハンドル側で状態を持つ)。
static std::vector<DeviceEntry> g_devices;
static std::string g_error;
static bool g_initialized = false;
#ifdef _WIN32
static bool g_com_initialized = false;
// rdl_initialize/rdl_shutdown は同一スレッド(Pythonメイン)からの直列呼び出し
// 前提(元々の前提)。discovery 常駐化により COM が shutdown/init をまたいで
// 維持され続けるようになったため、この前提が一時的でなく恒常的に効くようになった。
#endif

// デバイス増減(ホットプラグ)の世代カウンタ。DeckLink通知スレッドから++されるため
// atomic。shutdown→initialize をまたいで単調増加(Python側の前回値比較を壊さない)。
static std::atomic<int> g_device_generation{0};
// discovery はプロセス生存中は設置したまま(初回 initialize で1回だけ設置)。
// 再設置すると設置直後に「既存デバイスの到着通知」が一括で届き、Python側の
// 自動再検出(再init)→再設置→再通知… と自励発振するため、撤去しない。
static IDeckLinkDiscovery* g_discovery = nullptr;
static IDeckLinkDeviceNotificationCallback* g_notify_cb = nullptr;
// プロセス終了契約: rdl_shutdown 後も通知スレッドが本DLLへ入ってくることがあるが、
// コールバックは g_device_generation という atomic 1個を叩くだけであり、DLL自体は
// プロセス終了までアンロードされないため安全(ぶら下がり参照にはならない)。

IDeckLink* rdl_internal_device(int index) {  // rdl_api.cppから使う
    if (index < 0 || index >= (int)g_devices.size()) return nullptr;
    return g_devices[index].device;
}

static void copy_str(char* dst, size_t cap, const std::string& s) {
    if (cap == 0) return;
    size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    memcpy(dst, s.c_str(), n);
    dst[n] = '\0';
}

static void fill_modes(IDeckLink* dev, std::vector<RdlModeInfo>& out) {
    IDeckLinkOutput* o = nullptr;
    if (dev->QueryInterface(IID_IDeckLinkOutput, (void**)&o) != S_OK) return;
    IDeckLinkDisplayModeIterator* it = nullptr;
    if (o->GetDisplayModeIterator(&it) == S_OK) {
        IDeckLinkDisplayMode* m = nullptr;
        while (it->Next(&m) == S_OK) {
            RdlModeInfo mi{};
            mi.mode_id = m->GetDisplayMode();
            dlstring_t name = nullptr;
            if (m->GetName(&name) == S_OK) {
                copy_str(mi.name, sizeof(mi.name), dlstring_to_utf8(name));
                dlstring_free(name);
            }
            mi.width = m->GetWidth();
            mi.height = m->GetHeight();
            BMDTimeValue dur = 0;
            BMDTimeScale scale = 0;
            m->GetFrameRate(&dur, &scale);
            mi.fps_num = scale;
            mi.fps_den = dur;
            dlbool_t sup = 0;
            o->DoesSupportVideoMode(bmdVideoConnectionUnspecified, m->GetDisplayMode(),
                                    bmdFormat10BitRGB, bmdNoVideoOutputConversion,
                                    bmdSupportedVideoModeDefault, nullptr, &sup);
            mi.supports_r210 = sup ? 1 : 0;
            sup = 0;
            o->DoesSupportVideoMode(bmdVideoConnectionUnspecified, m->GetDisplayMode(),
                                    bmdFormat10BitYUV, bmdNoVideoOutputConversion,
                                    bmdSupportedVideoModeDefault, nullptr, &sup);
            mi.supports_v210 = sup ? 1 : 0;
            out.push_back(mi);
            m->Release();
        }
        it->Release();
    }
    o->Release();
}

namespace {
// 到着/除去でカウンタを進めるだけのコールバック。DeckLink API の通知スレッドから
// 呼ばれるため、デバイスリスト(g_devices)には一切触らない(atomic 1個のみ)。
class DeviceNotificationCallback : public IDeckLinkDeviceNotificationCallback {
public:
    HRESULT STDMETHODCALLTYPE DeckLinkDeviceArrived(IDeckLink*) override {
        ++g_device_generation;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DeckLinkDeviceRemoved(IDeckLink*) override {
        ++g_device_generation;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (rdl_iid_equal(riid, RDL_IID_IUNKNOWN) ||
            rdl_iid_equal(riid, IID_IDeckLinkDeviceNotificationCallback)) {
            *ppv = static_cast<IDeckLinkDeviceNotificationCallback*>(this);
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
    std::atomic<ULONG> ref_{1};
};
}  // namespace

// 初回のみ discovery を設置する。失敗は非致命(自動検出なしで従来どおり動く)。
static void install_discovery_once() {
    if (g_discovery) return;
#ifdef _WIN32
    IDeckLinkDiscovery* d = nullptr;
    if (CoCreateInstance(CLSID_CDeckLinkDiscovery, nullptr, CLSCTX_ALL,
                         IID_IDeckLinkDiscovery, (void**)&d) != S_OK || !d)
        return;
#else
    IDeckLinkDiscovery* d = CreateDeckLinkDiscoveryInstance();
    if (!d) return;
#endif
    auto* cb = new (std::nothrow) DeviceNotificationCallback();
    if (!cb || d->InstallDeviceNotifications(cb) != S_OK) {
        if (cb) cb->Release();
        d->Release();
        return;
    }
    g_notify_cb = cb;  // 自前の参照を保持(Install 側の保持実装に依存しない)
    g_discovery = d;
}

static int initialize_impl(void) {
    if (g_initialized) return RDL_OK;
#ifdef _WIN32
    if (!g_com_initialized) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            g_error = "CoInitializeEx failed";
            return RDL_E_INIT;
        }
        g_com_initialized = SUCCEEDED(hr);
    }
    IDeckLinkIterator* it = nullptr;
    if (CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
                         IID_IDeckLinkIterator, (void**)&it) != S_OK) {
        g_error = "DeckLink driver not found (is Desktop Video installed?)";
        return RDL_E_INIT;
    }
#else
    // macOS: COMは存在しない。SDK同梱 DeckLinkAPIDispatch.cpp のファクトリを使う
    // (実行時に DeckLinkAPI.framework を動的ロード。未インストールなら nullptr)。
    IDeckLinkIterator* it = CreateDeckLinkIteratorInstance();
    if (!it) {
        g_error = "DeckLink driver not found (is Desktop Video installed?)";
        return RDL_E_INIT;
    }
#endif
    IDeckLink* dev = nullptr;
    while (it->Next(&dev) == S_OK) {
        DeviceEntry e;
        e.device = dev;  // 参照は保持(shutdownでRelease)
        dlstring_t name = nullptr;
        if (dev->GetDisplayName(&name) == S_OK) {
            copy_str(e.info.name, sizeof(e.info.name), dlstring_to_utf8(name));
            dlstring_free(name);
        }
        IDeckLinkProfileAttributes* attr = nullptr;
        if (dev->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&attr) == S_OK) {
            int64_t conns = 0;
            if (attr->GetInt(BMDDeckLinkVideoOutputConnections, &conns) == S_OK) {
                e.info.has_sdi_output = (conns & bmdVideoConnectionSDI) ? 1 : 0;
                e.info.has_hdmi_output = (conns & bmdVideoConnectionHDMI) ? 1 : 0;
            }
            attr->Release();
        }
        fill_modes(dev, e.modes);
        g_devices.push_back(e);
    }
    it->Release();
    install_discovery_once();
    g_initialized = true;
    return RDL_OK;
}

extern "C" {

RDL_EXPORT int rdl_initialize(void) {
    try {
        return initialize_impl();
    } catch (...) {
        g_error = "internal error during initialization";
        return RDL_E_INTERNAL;
    }
}

RDL_EXPORT void rdl_shutdown(void) {
    for (auto& e : g_devices)
        if (e.device) e.device->Release();
    g_devices.clear();
    g_initialized = false;
#ifdef _WIN32
    // discovery 設置済みの間は COM を維持する(通知コールバックが生きているため)。
    if (g_com_initialized && !g_discovery) {
        CoUninitialize();
        g_com_initialized = false;
    }
#endif
}

RDL_EXPORT int rdl_get_device_count(void) { return (int)g_devices.size(); }

RDL_EXPORT int rdl_get_device_generation(void) {
    return g_device_generation.load();
}

RDL_EXPORT int rdl_get_device_info(int i, RdlDeviceInfo* out) {
    if (!out || i < 0 || i >= (int)g_devices.size()) return RDL_E_BADARG;
    *out = g_devices[i].info;
    return RDL_OK;
}

RDL_EXPORT int rdl_get_mode_count(int i) {
    if (i < 0 || i >= (int)g_devices.size()) return RDL_E_BADARG;
    return (int)g_devices[i].modes.size();
}

RDL_EXPORT int rdl_get_mode_info(int i, int mi, RdlModeInfo* out) {
    if (!out || i < 0 || i >= (int)g_devices.size()) return RDL_E_BADARG;
    if (mi < 0 || mi >= (int)g_devices[i].modes.size()) return RDL_E_BADARG;
    *out = g_devices[i].modes[mi];
    return RDL_OK;
}

RDL_EXPORT int rdl_get_last_error_global(char* buf, int len) {
    if (!buf || len <= 0) return RDL_E_BADARG;
    copy_str(buf, (size_t)len, g_error);
    return RDL_OK;
}

}  // extern "C"
