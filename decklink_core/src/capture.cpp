// decklink_core/src/capture.cpp — HDMI ループバック取り込み(設計書 §4)。
//
// 実測で確認済みの落とし穴(2026-07-26、設計書 §4.2〜§4.4):
// - 入力コネクタの既定は SDI。bmdDeckLinkConfigVideoInputConnection を
//   明示設定しないと無信号。
// - IDeckLinkConfiguration を解放すると設定が戻る。SetInt が S_OK を返した
//   直後に Release すると、エラーも警告も無いまま無信号になる。close まで保持。
// - 画素バッファは IDeckLinkVideoBuffer 経由(frame->GetBytes は SDK 16.0 に無い)。
#include "capture.h"

#include <cstring>

namespace {
void copy_fixed(char* dst, size_t cap, const std::string& s) {
    size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    memcpy(dst, s.c_str(), n);
    dst[n] = '\0';
}
}  // namespace

CaptureEngine::CaptureEngine(IDeckLink* dev, int64_t connection) {
    device_ = dev;
    device_->AddRef();   // g_devices の解放(rdl_shutdown)と寿命を切り離す
    if (dev->QueryInterface(IID_IDeckLinkInput, (void**)&input_) != S_OK) {
        input_ = nullptr;
        set_error("device has no capture interface (IDeckLinkInput)");
        return;
    }
    if (dev->QueryInterface(IID_IDeckLinkConfiguration, (void**)&config_) != S_OK) {
        config_ = nullptr;
        set_error("IDeckLinkConfiguration unavailable");
        return;
    }
    if (config_->SetInt(bmdDeckLinkConfigVideoInputConnection, connection) != S_OK)
        set_error("failed to select input connection");
    // 検証のため設定を読み戻す(効いていない静かな失敗の検出)。GetInt 自体が
    // 失敗した場合は読み戻しできないだけなので従来どおりソフト失敗(set_error のみ)。
    // 一方 GetInt が成功したのに値が食い違う場合は「切り替えたつもりが無効だった」
    // 実測済みの故障モード(このファイル冒頭コメント)そのものなので、construction を
    // 確実に失敗させる(input_ を破棄して ok()==false → rdl_input_open が nullptr を返す)。
    int64_t got = 0;
    if (config_->GetInt(bmdDeckLinkConfigVideoInputConnection, &got) == S_OK &&
        got != connection) {
        set_error("input connection did not take effect");
        input_->Release();
        input_ = nullptr;
        return;
    }
    info_.eotf = -1;
    info_.max_cll = info_.max_fall = info_.max_dml = info_.min_dml = -1.0;
}

CaptureEngine::~CaptureEngine() {
    stop();
    if (config_) config_->Release();
    if (input_) input_->Release();
    if (device_) device_->Release();
}

void CaptureEngine::set_error(const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    error_ = msg;
}

std::string CaptureEngine::last_error() {
    std::lock_guard<std::mutex> lk(mu_);
    return error_;
}

int CaptureEngine::start() {
    if (!ok()) return RDL_E_INTERNAL;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (started_) return RDL_E_BUSY;
    }
    input_->SetCallback(this);
    // 検出前の仮モード。VideoInputFormatChanged が実信号に合わせて張り替える。
    HRESULT hr = input_->EnableVideoInput(bmdModeHD1080p2997, bmdFormat10BitRGB,
                                          bmdVideoInputEnableFormatDetection);
    if (hr != S_OK) {
        input_->SetCallback(nullptr);
        set_error("EnableVideoInput failed (format detection unsupported?)");
        return RDL_E_UNSUPPORTED;
    }
    if (input_->StartStreams() != S_OK) {
        input_->DisableVideoInput();
        input_->SetCallback(nullptr);
        set_error("StartStreams failed");
        return RDL_E_INTERNAL;
    }
    std::lock_guard<std::mutex> lk(mu_);
    started_ = true;
    stopping_ = false;   // stop→start 再開のためのリセット(一方向ラッチにしない)
    return RDL_OK;
}

// teardown は playback.cpp の PlaybackEngine::stop() と同じ方針:
// コールバックは SDK スレッドから任意のタイミングで届き得るため、オブジェクト自体の
// 参照カウントではなく「共有状態を mu_ で守り、stop() 完了 = 進行中コールバックが
// mu_ 内の処理を終えて抜けたこと」を根拠に安全な破棄を成立させる。
//   1) stopping_=true を立てて cv_ を起こす(grab() 待機中なら即座に NOSIGNAL で返す)。
//   2) mu_ を離してから StopStreams/DisableVideoInput/SetCallback(nullptr)。
//      (SDK 呼び出し中に mu_ を保持すると、コールバック側の mu_ 取得とデッドロックし得る)
//   3) 最後に mu_ を再取得して即座に手放す — この再取得が、呼び出し中に mu_ を
//      握っていたコールバックの完了を待つ「ドレイン」になる。
// 残存する隙間(コールバックが SDK 内でディスパッチ済みだが、まだ mu_ に到達していない
// 区間)は playback.cpp 側と同様に許容する(ステップ2の SetCallback(nullptr) 以降は
// 新規ディスパッチが起きないため、この隙間はごく短い)。
// このレース挙動自体(stopping_ の遷移・コールバックの早期リターン)は実機の DeckLink
// SDK スレッドが無いと単体テストで再現できない。挙動レベルのカバレッジは Task 4 の
// ワーカ層テスト(フェイク DLL でコールバックタイミングを模擬)で行う想定。
int CaptureEngine::stop() {
    if (!input_) return RDL_OK;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!started_) return RDL_OK;
        started_ = false;
        stopping_ = true;
        cv_.notify_all();   // grab() が待機中なら起こして NOSIGNAL で返す
    }
    input_->StopStreams();
    input_->DisableVideoInput();
    input_->SetCallback(nullptr);
    { std::lock_guard<std::mutex> lk(mu_); }   // ドレイン: 進行中コールバックの完了待ち
    return RDL_OK;
}

int CaptureEngine::grab(int skip_frames, int timeout_ms, void* buf, int64_t size,
                        RdlCapturedInfo* out) {
    if (!buf || size <= 0 || !out || skip_frames < 0) return RDL_E_BADARG;
    std::unique_lock<std::mutex> lk(mu_);
    if (!started_ || stopping_) return RDL_E_INIT;
    want_skip_ = skip_frames;
    want_buf_ = buf;
    want_size_ = size;
    grabbed_ = false;
    grab_badarg_ = false;
    bool done = cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                             [this] { return grabbed_ || grab_badarg_ || stopping_; });
    want_skip_ = -1;    // 要求を確実に閉じる(遅延フレームの誤書き込み防止)
    want_buf_ = nullptr;
    // out には可能な限り info_ を返す(バッファ不足時の必要サイズ通知に使う。
    // rdl_api.h の rdl_input_grab の契約: 「buf が小さければ RDL_E_BADARG
    // (必要サイズは out->row_bytes * out->height)」— コールバックは badarg 判定前に
    // width/height/row_bytes を info_ へ書き込み済みなので、ここでコピーすれば足りる)。
    *out = info_;
    if (stopping_ && !grabbed_ && !grab_badarg_) {
        error_ = "stopped";
        return RDL_E_NOSIGNAL;
    }
    if (grab_badarg_) {
        error_ = "buffer too small for captured frame";
        return RDL_E_BADARG;
    }
    if (!done) {
        error_ = "no input signal (timeout)";
        return RDL_E_NOSIGNAL;
    }
    return RDL_OK;
}

HRESULT CaptureEngine::VideoInputFormatChanged(
        BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode* mode,
        BMDDetectedVideoInputFormatFlags flags) {
    if (!mode) return S_OK;
    {
        // stop() 進行中/済みなら SDK へ触れない(teardown 中の PauseStreams 等は
        // stop() 側の StopStreams/DisableVideoInput と競合し得る)。
        std::lock_guard<std::mutex> lk(mu_);
        if (stopping_) return S_OK;
    }
    // 検出結果に合わせて再設定(設計書 §4.3): RGB444 → r210 / それ以外 → v210
    // mu_ を保持したまま input_ を呼ばない(stop() のドレインとデッドロックする)。
    BMDPixelFormat want = (flags & bmdDetectedVideoInputRGB444)
                          ? bmdFormat10BitRGB : bmdFormat10BitYUV;
    input_->PauseStreams();
    input_->EnableVideoInput(mode->GetDisplayMode(), want,
                             bmdVideoInputEnableFormatDetection);
    input_->FlushStreams();
    input_->StartStreams();
    std::lock_guard<std::mutex> lk(mu_);
    if (stopping_) return S_OK;   // 上記 SDK 呼び出し中に stop() が進んだ場合の再確認
    info_.mode_id = (int64_t)mode->GetDisplayMode();
    info_.detected_flags = (int64_t)flags;
    dlstring_t nm = nullptr;
    if (mode->GetName(&nm) == S_OK) {
        copy_fixed(info_.mode_name, sizeof(info_.mode_name), dlstring_to_utf8(nm));
        dlstring_free(nm);
    }
    return S_OK;
}

HRESULT CaptureEngine::VideoInputFrameArrived(
        IDeckLinkVideoInputFrame* frame, IDeckLinkAudioInputPacket*) {
    if (!frame) return S_OK;
    if (frame->GetFlags() & bmdFrameHasNoInputSource) return S_OK;

    std::unique_lock<std::mutex> lk(mu_);
    if (stopping_) return S_OK;   // stop() 進行中/済み — 共有状態に触れない
    if (want_skip_ < 0 || grabbed_) return S_OK;   // grab 要求なし
    if (want_skip_ > 0) { --want_skip_; return S_OK; }

    info_.width = frame->GetWidth();
    info_.height = frame->GetHeight();
    info_.row_bytes = frame->GetRowBytes();
    info_.pixel_format = (int64_t)frame->GetPixelFormat();
    // I3 修正（2026-07-26 最終レビュー）: detected_flags は本来
    // VideoInputFormatChanged コールバックでのみ埋まる。しかし着信信号が
    // 既に construction 時の仮モード(bmdModeHD1080p2997 + bmdFormat10BitRGB)
    // と一致している場合、SDK はフォーマット変化と見なさずこのコールバックを
    // 一度も発火しないことがある — その場合 detected_flags が 0 のままになり、
    // verify 側は「ワイヤが RGB444 でない」という偽の INVALID を報告してしまう
    // (実際には RGB444 で来ている)。ここでフレームの pixel_format から
    // フォールバックの flags を導出する(mode_id/mode_name のフォールバックは
    // 判定に使われないため対象外 — 判定が依存するのは flags のみ)。
    if (info_.detected_flags == 0) {
        if (info_.pixel_format == bmdFormat10BitRGB) {
            info_.detected_flags = bmdDetectedVideoInputRGB444
                                   | bmdDetectedVideoInput10BitDepth;
        } else if (info_.pixel_format == bmdFormat10BitYUV) {
            info_.detected_flags = bmdDetectedVideoInputYCbCr422
                                   | bmdDetectedVideoInput10BitDepth;
        }
    }
    int64_t need = info_.row_bytes * info_.height;
    if (need > want_size_) {
        grab_badarg_ = true;
        cv_.notify_all();
        return S_OK;
    }

    // HDR メタデータ(設計書 §4.4。属性名は SDK 16.0 の実綴り)
    info_.hdr_present = (frame->GetFlags() & bmdFrameContainsHDRMetadata) ? 1 : 0;
    info_.eotf = -1;
    info_.max_cll = info_.max_fall = info_.max_dml = info_.min_dml = -1.0;
    IDeckLinkVideoFrameMetadataExtensions* meta = nullptr;
    if (frame->QueryInterface(IID_IDeckLinkVideoFrameMetadataExtensions,
                              (void**)&meta) == S_OK && meta) {
        int64_t v = 0;
        if (meta->GetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc,
                         &v) == S_OK)
            info_.eotf = v;
        double d = 0;
        if (meta->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel,
                           &d) == S_OK) info_.max_cll = d;
        if (meta->GetFloat(
                bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel,
                &d) == S_OK) info_.max_fall = d;
        if (meta->GetFloat(
                bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance,
                &d) == S_OK) info_.max_dml = d;
        if (meta->GetFloat(
                bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance,
                &d) == S_OK) info_.min_dml = d;
        meta->Release();
    }

    // 画素コピー(IDeckLinkVideoBuffer 経由 — playback.cpp と同じ流儀)
    IDeckLinkVideoBuffer* vbuf = nullptr;
    if (frame->QueryInterface(IID_IDeckLinkVideoBuffer, (void**)&vbuf) == S_OK
            && vbuf) {
        void* bytes = nullptr;
        if (vbuf->StartAccess(bmdBufferAccessRead) == S_OK) {
            if (vbuf->GetBytes(&bytes) == S_OK && bytes) {
                memcpy(want_buf_, bytes, (size_t)need);
                grabbed_ = true;
            }
            vbuf->EndAccess(bmdBufferAccessRead);
        }
        vbuf->Release();
    }
    if (grabbed_) cv_.notify_all();
    return S_OK;
}

HRESULT CaptureEngine::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (rdl_iid_equal(riid, RDL_IID_IUNKNOWN) ||
        rdl_iid_equal(riid, IID_IDeckLinkInputCallback)) {
        *ppv = static_cast<IDeckLinkInputCallback*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
// 寿命は rdl_input_open/close が所有する(COM 参照カウントに委ねない —
// playback.cpp の PlaybackEngine と同じ方針)。
ULONG CaptureEngine::AddRef() { return 1; }
ULONG CaptureEngine::Release() { return 1; }
