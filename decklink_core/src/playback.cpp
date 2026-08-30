// 流用元: 20260611_rawdecklink-signal-player/core/src/playback.cpp
// 改変点(Phase 7 Task 2): メモリバッファ再生モードを追加 —
//   start() の本体を start_impl() に一般化(ファイルモードは従来どおり)、
//   start_memory()/update_frame() 追加、read_into/next_file_frame/schedule_one/
//   get_status/stop にメモリモード分岐。ファイル再生系の既存動作は不変。
// core/src/playback.cpp
#include "playback.h"

#include <chrono>
#include <cstring>

#include "crc32.h"

PlaybackEngine::PlaybackEngine(IDeckLink* device) : device_(device) { device_->AddRef(); }

PlaybackEngine::~PlaybackEngine() {
    stop();
    if (device_) device_->Release();
}

HRESULT PlaybackEngine::QueryInterface(REFIID, LPVOID*) { return E_NOINTERFACE; }
ULONG PlaybackEngine::AddRef() { return 1; }
ULONG PlaybackEngine::Release() { return 1; }

void PlaybackEngine::fail(const std::string& msg) {
    { std::lock_guard<std::mutex> lk(error_mutex_); error_ = msg; }
    state_ = RDL_STATE_ERROR;
    // エラー時はワーカを起こして速やかにループを抜けさせる
    stop_workers_ = true;
    free_cv_.notify_all();
    filled_cv_.notify_all();
}

std::string PlaybackEngine::last_error() const {
    std::lock_guard<std::mutex> lk(error_mutex_);
    return error_;
}

int64_t PlaybackEngine::next_file_frame(int64_t seq) const {
    int64_t n = memory_mode_ ? 1 : reader_->frame_count();  // メモリモードは常に1フレーム
    if (n == 1) return 0;                      // 静止画相当: 同一フレームをホールド
    if (seq < n) return seq;
    return loop_ ? seq % n : n - 1;            // 非ループは最終フレームをホールド
}

// readerスレッド専用: frame バッファへ file_idx のフレームを読み込む。
// CRCはschedulerスレッドが負担し、read(2GB/s)とCRC(2GB/s)を別スレッドへ振り分ける。
bool PlaybackEngine::read_into(IDeckLinkMutableVideoFrame* frame, int64_t file_idx) {
    // SDK 16.0: フレームバッファへのアクセスは IDeckLinkVideoBuffer 経由。
    IDeckLinkVideoBuffer* vbuf = nullptr;
    if (frame->QueryInterface(IID_IDeckLinkVideoBuffer, (void**)&vbuf) != S_OK || !vbuf) {
        fail("cannot get IDeckLinkVideoBuffer from frame");
        return false;
    }
    void* bytes = nullptr;
    bool started = (vbuf->StartAccess(bmdBufferAccessWrite) == S_OK);
    if (!started || vbuf->GetBytes(&bytes) != S_OK || !bytes) {
        if (started) vbuf->EndAccess(bmdBufferAccessWrite);
        vbuf->Release();
        fail("cannot access frame buffer bytes");
        return false;
    }
    if (memory_mode_) {
        // メモリモード: 現行フレーム(mem_front_)をコピー。update_frame の swap と排他。
        std::lock_guard<std::mutex> lk(mem_mutex_);
        memcpy(bytes, mem_front_.data(), (size_t)mem_frame_size_);
    } else {
        try {
            reader_->read_frame(file_idx, bytes);
        } catch (const std::exception& e) {
            vbuf->EndAccess(bmdBufferAccessWrite);
            vbuf->Release();
            fail(e.what());
            return false;
        }
    }
    vbuf->EndAccess(bmdBufferAccessWrite);
    vbuf->Release();
    return true;
}

// フレームへ HDR/色域メタデータを設定する。成功で true。
// 非対応(QueryInterface 失敗)や hdr_present=0 のときは false(=skip)。
static bool apply_hdr_metadata(IDeckLinkMutableVideoFrame* frame,
                               const RdlPlaybackConfig& cfg) {
    if (cfg.hdr_present == 0) return false;
    IDeckLinkVideoFrameMutableMetadataExtensions* ext = nullptr;
    if (frame->QueryInterface(IID_IDeckLinkVideoFrameMutableMetadataExtensions,
                              (void**)&ext) != S_OK || !ext) {
        return false;   // 機種非対応: 設計書 §3-C-2(BVM側手動運用)
    }
    if (cfg.colorspace != 0)
        ext->SetInt(bmdDeckLinkFrameMetadataColorspace, cfg.colorspace);
    ext->SetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc, cfg.eotf);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX, cfg.prim_red_x);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY, cfg.prim_red_y);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX, cfg.prim_green_x);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY, cfg.prim_green_y);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX, cfg.prim_blue_x);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY, cfg.prim_blue_y);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRWhitePointX, cfg.white_x);
    ext->SetFloat(bmdDeckLinkFrameMetadataHDRWhitePointY, cfg.white_y);
    // 輝度メタは EOTF=PQ(2) のときのみ(設計書 Global Constraints)。
    if (cfg.eotf == 2) {
        ext->SetFloat(bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance, cfg.max_dml);
        ext->SetFloat(bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance, cfg.min_dml);
        ext->SetFloat(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel, cfg.max_cll);
        ext->SetFloat(bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel, cfg.max_fall);
    }
    ext->Release();
    return true;
}

// schedulerスレッド専用: SDKに渡すバッファそのもののCRC(レベル1 bit exactの根拠)を
// 計算してringへ記録し、そのままScheduleVideoFrameに渡す
// ("SDKに手渡す内容のCRC"という性質を維持。CRCはScheduleの直前)。
bool PlaybackEngine::schedule_one(const FilledItem& item) {
    IDeckLinkVideoBuffer* vbuf = nullptr;
    if (item.frame->QueryInterface(IID_IDeckLinkVideoBuffer, (void**)&vbuf) != S_OK || !vbuf) {
        fail("cannot get IDeckLinkVideoBuffer from frame");
        return false;
    }
    void* bytes = nullptr;
    bool started = (vbuf->StartAccess(bmdBufferAccessRead) == S_OK);
    if (!started || vbuf->GetBytes(&bytes) != S_OK || !bytes) {
        if (started) vbuf->EndAccess(bmdBufferAccessRead);
        vbuf->Release();
        fail("cannot access frame buffer bytes");
        return false;
    }
    uint32_t crc = rdl_crc32(0, bytes, (size_t)src_frame_size());
    vbuf->EndAccess(bmdBufferAccessRead);
    vbuf->Release();
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        RdlFrameCrc& e = ring_[ring_write_ % kRing];
        e.sequence = item.seq;
        e.file_frame = item.file_idx;
        e.crc32 = crc;
        ++ring_write_;
        if (ring_write_ - ring_read_ > kRing) ring_read_ = ring_write_ - kRing;
    }
    current_frame_ = item.file_idx;
    if (output_->ScheduleVideoFrame(item.frame, item.seq * frame_duration_, frame_duration_,
                                    time_scale_) != S_OK) {
        fail("ScheduleVideoFrame failed");
        return false;
    }
    scheduled_ = item.seq + 1;
    return true;
}

void PlaybackEngine::reader_loop() {
    for (;;) {
        IDeckLinkMutableVideoFrame* frame = nullptr;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            free_cv_.wait(lk, [&] { return stop_workers_ || !free_.empty(); });
            if (stop_workers_) return;
            frame = free_.front();
            free_.pop_front();
        }
        int64_t seq = seq_.fetch_add(1);
        int64_t file_idx = next_file_frame(seq);
        if (!read_into(frame, file_idx)) return;  // fail()済み。stop_workers_も立つ
        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            filled_.push_back({frame, seq, file_idx});
        }
        filled_cv_.notify_one();
    }
}

void PlaybackEngine::scheduler_loop() {
    for (;;) {
        FilledItem item;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            filled_cv_.wait(lk, [&] { return stop_workers_ || !filled_.empty(); });
            if (stop_workers_) return;
            item = filled_.front();
            filled_.pop_front();
        }
        if (!schedule_one(item)) return;
    }
}

int PlaybackEngine::start(const RdlPlaybackConfig& cfg) {
    return start_impl(cfg, nullptr, 0);
}

int PlaybackEngine::start_memory(const RdlPlaybackConfig& cfg,
                                 const void* frame_bytes, int64_t size) {
    if (!frame_bytes || size <= 0) return RDL_E_BADARG;
    return start_impl(cfg, frame_bytes, size);
}

int PlaybackEngine::start_impl(const RdlPlaybackConfig& cfg,
                               const void* mem_bytes, int64_t mem_size) {
    if (state_ == RDL_STATE_PLAYING) return RDL_E_BUSY;
    if (device_->QueryInterface(IID_IDeckLinkOutput, (void**)&output_) != S_OK) {
        fail("cannot get IDeckLinkOutput");
        return RDL_E_INTERNAL;
    }
    // モード情報(幅・高さ・フレームレート)を取得
    int64_t width = 0, height = 0;
    bool mode_found = false;
    IDeckLinkDisplayModeIterator* it = nullptr;
    if (output_->GetDisplayModeIterator(&it) == S_OK) {
        IDeckLinkDisplayMode* m = nullptr;
        while (it->Next(&m) == S_OK) {
            if (m->GetDisplayMode() == (BMDDisplayMode)cfg.mode_id) {
                m->GetFrameRate(&frame_duration_, &time_scale_);
                width = m->GetWidth();
                height = m->GetHeight();
                mode_found = true;
                m->Release();
                break;
            }
            m->Release();
        }
        it->Release();
    }
    if (!mode_found) {
        fail("display mode not found on this device");
        release_output();
        return RDL_E_UNSUPPORTED;
    }

    BMDPixelFormat pixfmt = (BMDPixelFormat)cfg.pixel_format;
    dlbool_t supported = 0;
    if (output_->DoesSupportVideoMode(bmdVideoConnectionUnspecified,
            (BMDDisplayMode)cfg.mode_id, pixfmt, bmdNoVideoOutputConversion,
            bmdSupportedVideoModeDefault, nullptr, &supported) != S_OK || !supported) {
        fail("mode/pixel-format combination not supported");
        release_output();
        return RDL_E_UNSUPPORTED;
    }

    if (mem_bytes) {
        // メモリソースモード: RawReader と同一式(rdl_row_bytes(fourcc,width)*height)で
        // 1 フレームサイズを計算し、呼び出し側 size と一致しなければ BADARG。
        int64_t expect = 0;
        try {
            expect = rdl_row_bytes((uint32_t)cfg.pixel_format, width) * height;
        } catch (const std::exception& e) {
            fail(e.what());
            release_output();
            return RDL_E_BADARG;
        }
        if (mem_size != expect) {
            fail("memory frame size " + std::to_string(mem_size) +
                 " does not match mode frame size " + std::to_string(expect));
            release_output();
            return RDL_E_BADARG;
        }
        {
            std::lock_guard<std::mutex> lk(mem_mutex_);
            const uint8_t* p = static_cast<const uint8_t*>(mem_bytes);
            mem_front_.assign(p, p + mem_size);
            mem_back_.assign(p, p + mem_size);
        }
        mem_frame_size_ = expect;
        memory_mode_ = true;
    } else {
        memory_mode_ = false;
        try {
            reader_ = std::make_unique<RawReader>(
                std::string(cfg.raw_path_utf8), width, height, (uint32_t)cfg.pixel_format);
        } catch (const std::exception& e) {
            fail(e.what());
            release_output();
            return RDL_E_FILE;
        }
    }

    // SDI出力構成を選択フォーマットに合わせて設定(再生開始毎)。揮発SetFlagは実ワイヤーに
    // 効かないため WriteConfigurationToPreferences で恒久保存する(444/422のみ自動連動)。
    // Link/Level/Default mode は現在値を維持(手動設定を尊重)。設定失敗は非致命。
    {
        IDeckLinkConfiguration* config = nullptr;
        if (device_->QueryInterface(IID_IDeckLinkConfiguration,
                                    (void**)&config) == S_OK && config) {
            bool want444 = (pixfmt == bmdFormat10BitRGB);
            config->SetFlag(bmdDeckLinkConfig444SDIVideoOutput, want444);
            config->WriteConfigurationToPreferences();   // 恒久保存(実機で唯一効く)
            dlbool_t rb444 = 0;
            int64_t rblink = 0;
            applied_444_ = (config->GetFlag(bmdDeckLinkConfig444SDIVideoOutput,
                                            &rb444) == S_OK) ? (rb444 ? 1 : 0) : -1;
            applied_link_ = (config->GetInt(bmdDeckLinkConfigSDIOutputLinkConfiguration,
                                            &rblink) == S_OK) ? (int64_t)rblink : -1;
            config->Release();
        }
    }

    if (output_->EnableVideoOutput((BMDDisplayMode)cfg.mode_id,
                                   bmdVideoOutputFlagDefault) != S_OK) {
        fail("EnableVideoOutput failed (device busy?)");
        release_output();
        return RDL_E_BUSY;
    }
    output_->SetScheduledFrameCompletionCallback(this);

    loop_ = cfg.loop_playback != 0;
    seq_ = 0;
    scheduled_ = 0;
    completed_ = dropped_ = late_ = 0;
    current_frame_ = 0;
    stop_workers_ = false;
    {
        std::lock_guard<std::mutex> lk(ring_mutex_);
        ring_write_ = ring_read_ = 0;
    }

    // プリロール数: cfg未指定(0)なら 2GB/s 維持のため既定8、指定時は尊重。最小3。
    int64_t preroll = cfg.preroll_frames > 0 ? cfg.preroll_frames : 8;
    if (preroll < 3) preroll = 3;
    // プール(同時に巡回するフレーム数)はパイプライン段数を決める。
    // - ファイル再生: プリロールが浅くても reader/scheduler/SDK の3段を常に
    //   充填できるよう最低8枚を確保する(2GB/s維持に必須)。
    // - メモリモード(静止フレームのライブ差し替え): 充填は memcpy のみで
    //   スループット要件が無く、プール枚数 ≒ 機内に浮くフレーム数 ＝
    //   update_frame の反映レイテンシになる(実測: プール8×23.98fps ≈ 0.33秒
    //   遅延 — 2026-07-27)。preroll+1 まで絞る(preroll≥3 なので最小4枚)。
    int64_t pool_count = mem_bytes ? (preroll + 1)
                                   : (preroll < 8 ? 8 : preroll);
    int64_t row_bytes = rdl_row_bytes((uint32_t)cfg.pixel_format, width);
    BMDFrameFlags frame_flags = (cfg.hdr_present != 0)
        ? bmdFrameContainsHDRMetadata : bmdFrameFlagDefault;
    int64_t hdr_ok = -1;   // -1=未試行
    {
        // 防御: 前回 stop() のロック済みクリア後に、ごく遅延したコールバックが
        // 解放済みポインタを free_ へ push した可能性を掃除してから構築する
        // (この時点で出力は無効でコールバックは走らないため安全)。
        std::lock_guard<std::mutex> lk(queue_mutex_);
        free_.clear();
        filled_.clear();
    }
    for (int64_t i = 0; i < pool_count; ++i) {
        IDeckLinkMutableVideoFrame* f = nullptr;
        if (output_->CreateVideoFrame((int32_t)width, (int32_t)height, (int32_t)row_bytes,
                                      pixfmt, frame_flags, &f) != S_OK) {
            fail("CreateVideoFrame failed");
            stop();
            return RDL_E_INTERNAL;
        }
        if ((int64_t)f->GetRowBytes() * height != src_frame_size()) {
            f->Release();
            fail("DeckLink frame pitch differs from file pitch");
            stop();
            return RDL_E_UNSUPPORTED;
        }
        if (cfg.hdr_present != 0)
            hdr_ok = apply_hdr_metadata(f, cfg) ? 1 : 0;
        pool_.push_back(f);
        free_.push_back(f);
    }
    applied_hdr_ = hdr_ok;

    state_ = RDL_STATE_PLAYING;  // ワーカ起動前に遷移(schedule系はPLAYING前提)

    // ワーカ起動。reader/scheduler が free→filled→schedule とプリロールを満たす。
    reader_thread_ = std::thread(&PlaybackEngine::reader_loop, this);
    scheduler_thread_ = std::thread(&PlaybackEngine::scheduler_loop, this);

    // プリロール分が実際に ScheduleVideoFrame されるまで待ってから再生開始。
    // ワーカがfail()するとstateがERRORに落ちるのでそこも抜ける。
    while (scheduled_.load() < preroll && state_ == RDL_STATE_PLAYING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (state_ != RDL_STATE_PLAYING) {  // プリロール中にワーカがfailした
        int rc = (last_error().find("read") != std::string::npos) ? RDL_E_FILE
                                                                   : RDL_E_INTERNAL;
        stop();
        return rc;
    }

    if (output_->StartScheduledPlayback(0, time_scale_, 1.0) != S_OK) {
        fail("StartScheduledPlayback failed");
        stop();
        return RDL_E_INTERNAL;
    }
    return RDL_OK;
}

HRESULT PlaybackEngine::ScheduledFrameCompleted(IDeckLinkVideoFrame* frame,
                                                BMDOutputFrameCompletionResult result) {
    // コールバックは集計とフレーム返却のみ。ファイルI/O・CRC・スケジュールは行わない。
    if (state_ != RDL_STATE_PLAYING) return S_OK;
    ++completed_;
    if (result == bmdOutputFrameDropped) ++dropped_;
    else if (result == bmdOutputFrameDisplayedLate) ++late_;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        free_.push_back(static_cast<IDeckLinkMutableVideoFrame*>(frame));
    }
    free_cv_.notify_one();
    return S_OK;
}

HRESULT PlaybackEngine::ScheduledPlaybackHasStopped() { return S_OK; }

// 任意スレッド(Python側)から呼ばれる。全処理を mem_mutex_ 下で行い、
// reader_loop の memcpy(mem_front_)・stop() のバッファクリアと排他する。
// stop() は state_ を先に PLAYING 以外へ変えてからクリアするため、
// ここで state/サイズ検査を通過した時点でバッファは有効なまま。
int PlaybackEngine::update_frame(const void* frame_bytes, int64_t size) {
    if (!frame_bytes || size <= 0) return RDL_E_BADARG;
    std::lock_guard<std::mutex> lk(mem_mutex_);
    if (state_ != RDL_STATE_PLAYING || !memory_mode_) return RDL_E_BADARG;
    if (size != mem_frame_size_ || (int64_t)mem_back_.size() != size) return RDL_E_BADARG;
    memcpy(mem_back_.data(), frame_bytes, (size_t)size);
    mem_front_.swap(mem_back_);   // 以降の充填から新フレーム(O(1)ポインタ交換)
    return RDL_OK;
}

void PlaybackEngine::release_output() {
    if (output_) {
        output_->Release();
        output_ = nullptr;
    }
}

void PlaybackEngine::join_workers() {
    stop_workers_ = true;
    free_cv_.notify_all();
    filled_cv_.notify_all();
    if (reader_thread_.joinable()) reader_thread_.join();
    if (scheduler_thread_.joinable()) scheduler_thread_.join();
}

int PlaybackEngine::stop() {
    if (!output_) return RDL_OK;
    if (state_ != RDL_STATE_ERROR) state_ = RDL_STATE_STOPPED;
    // 先にワーカを止めて join。これでファイル/CRC/スケジュールがSDKリソースに触れなくなる。
    join_workers();
    // 以降コールバックは state!=PLAYING で何もしない。StopScheduledPlaybackで打ち切り。
    output_->StopScheduledPlayback(0, nullptr, 0);
    output_->SetScheduledFrameCompletionCallback(nullptr);
    output_->DisableVideoOutput();
    // ScheduledFrameCompleted は state チェック通過直後にプリエンプトされると、
    // 上記の打ち切り後でも free_ へ push し得る(SDKスレッド)。deque への並行
    // アクセスは UB のため、クリアは必ず queue_mutex_ 下で行う。Release は
    // COM 呼び出しなのでロック外で実行する(pool_ をスワップして退避)。
    std::vector<IDeckLinkMutableVideoFrame*> to_release;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        to_release.swap(pool_);
        free_.clear();
        filled_.clear();
    }
    for (auto* f : to_release) f->Release();
    reader_.reset();
    // メモリソースの解放。並行する update_frame と排他するため mem_mutex_ 下で行う
    // (state_ は上で PLAYING 以外に変えてあるので、以降の update_frame は BADARG)。
    {
        std::lock_guard<std::mutex> lk(mem_mutex_);
        memory_mode_ = false;
        mem_frame_size_ = 0;
        mem_front_.clear();
        mem_front_.shrink_to_fit();
        mem_back_.clear();
        mem_back_.shrink_to_fit();
    }
    release_output();
    return RDL_OK;
}

void PlaybackEngine::get_status(RdlStatus* out) {
    out->state = state_;
    out->file_frame_count = memory_mode_ ? 1 : (reader_ ? reader_->frame_count() : 0);
    out->current_frame = current_frame_;
    out->completed_frames = completed_;
    out->dropped_frames = dropped_;
    out->late_frames = late_;
    out->scheduled_frames = scheduled_;
    out->sdi_444 = applied_444_;
    out->sdi_link = applied_link_;
    out->hdr_applied = applied_hdr_;
}

int PlaybackEngine::get_frame_crcs(RdlFrameCrc* buf, int max_count) {
    std::lock_guard<std::mutex> lk(ring_mutex_);
    int n = 0;
    while (n < max_count && ring_read_ < ring_write_) {
        buf[n++] = ring_[ring_read_ % kRing];
        ++ring_read_;
    }
    return n;
}
