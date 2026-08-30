// decklink_core/src/capture.h — HDMI ループバック取り込みエンジン(設計書 §4)。
// IDeckLinkInput のフォーマット自動検出で 1 枚を同期取得する。
#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

#include "platform_compat.h"
#include "rdl_api.h"

class CaptureEngine : public IDeckLinkInputCallback {
public:
    // dev は AddRef して保持(列挙側 g_devices の解放と寿命が競合しないように)。
    // connection の設定に使う IDeckLinkConfiguration も close まで保持する。
    CaptureEngine(IDeckLink* dev, int64_t connection);
    ~CaptureEngine();

    bool ok() const { return input_ != nullptr && config_ != nullptr; }
    int start();
    int grab(int skip_frames, int timeout_ms, void* buf, int64_t size,
             RdlCapturedInfo* out);
    int stop();
    std::string last_error();

    // IDeckLinkInputCallback
    HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
        BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode* mode,
        BMDDetectedVideoInputFormatFlags flags) override;
    HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
        IDeckLinkVideoInputFrame* frame, IDeckLinkAudioInputPacket* audio) override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

private:
    void set_error(const std::string& msg);

    IDeckLink* device_ = nullptr;
    IDeckLinkInput* input_ = nullptr;
    IDeckLinkConfiguration* config_ = nullptr;   // close まで保持(設計書 §4.2)

    std::mutex mu_;
    std::condition_variable cv_;
    // --- mu_ 保護下 ---
    bool started_ = false;
    bool stopping_ = false;         // stop() 進行中(playback.cpp の teardown 方針を踏襲)
    std::string error_;
    RdlCapturedInfo info_{};        // 直近のフォーマット検出結果を蓄積
    int   want_skip_ = -1;          // >=0 で grab 要求中(コールバックが減算)
    void* want_buf_ = nullptr;
    int64_t want_size_ = 0;
    bool  grabbed_ = false;         // 要求が満たされた
    bool  grab_badarg_ = false;     // バッファ不足で失敗した
};
