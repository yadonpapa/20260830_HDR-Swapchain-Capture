// 流用元: 20260611_rawdecklink-signal-player/core/src/playback.h
// 改変点(Phase 7 Task 2): メモリバッファ再生モードを追加 —
//   start_memory() / update_frame() とダブルバッファ(mem_front_/mem_back_)。
//   ファイル再生系の既存動作は不変。
// core/src/playback.h
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform_compat.h"
#include "raw_reader.h"
#include "rdl_api.h"

// rawファイルのスケジュール再生 + スケジュール直前CRC記録(spec §7-8)。
// 2160p59.94 r210 (~2GB/s) を維持するため、ファイル読み込み(readerスレッド)と
// CRC+スケジュール(schedulerスレッド)をSDKコールバックスレッドから分離する
// プロデューサ・パイプライン構成。
//   reader   : free-queue → (file read) → filled-queue
//   scheduler: filled-queue → (CRC + ScheduleVideoFrame)
//   callback : 完了カウントのみ + frame を free-queue へ返却
// IDeckLinkVideoOutputCallback はDeckLink SDKのスレッドから呼ばれる。
class PlaybackEngine : public IDeckLinkVideoOutputCallback {
public:
    explicit PlaybackEngine(IDeckLink* device);
    ~PlaybackEngine();

    int start(const RdlPlaybackConfig& cfg);
    // メモリバッファ再生(Phase 7): RawReader を開かず、呼び出し側が渡した
    // 1 フレーム分のバイト列を内部コピーして 1 フレームのループ再生として開始。
    // size がモード・pixel_format から計算したフレームサイズと不一致なら RDL_E_BADARG。
    int start_memory(const RdlPlaybackConfig& cfg, const void* frame_bytes, int64_t size);
    // 再生を止めずにフレーム内容を差し替える(RDL_STATE_PLAYING かつメモリモード中のみ)。
    // mem_back_ へコピーし mem_mutex_ 下で front/back を swap。以降に充填される
    // フレームから反映される(反映レイテンシはプール/プリロール分)。
    int update_frame(const void* frame_bytes, int64_t size);
    int stop();
    void get_status(RdlStatus* out);
    int get_frame_crcs(RdlFrameCrc* buf, int max_count);
    std::string last_error() const;

    // IUnknown(寿命はnew/deleteで管理するため参照カウントはダミー)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    // IDeckLinkVideoOutputCallback
    HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted(
        IDeckLinkVideoFrame* frame, BMDOutputFrameCompletionResult result) override;
    HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped() override;

private:
    // パイプライン: filled は (frame, seq, file_idx)。
    // read(2GB/s)はreaderスレッド、CRC(2GB/s)+ScheduleVideoFrameはschedulerスレッドへ
    // 振り分け、両者を別CPUで並列化して2160p59.94 r210の帯域を確保する。
    struct FilledItem {
        IDeckLinkMutableVideoFrame* frame;
        int64_t seq;
        int64_t file_idx;
    };

    // start()/start_memory() の共通本体。mem_bytes=nullptr ならファイルモード
    // (RawReader)、非 nullptr ならメモリモード(mem_size は 1 フレームのバイト数)。
    int start_impl(const RdlPlaybackConfig& cfg, const void* mem_bytes, int64_t mem_size);
    // 充填ソースの 1 フレームサイズ。メモリモードは mem_frame_size_(ワーカ起動前に
    // 確定・以後不変)、ファイルモードは reader_(従来どおり)。
    int64_t src_frame_size() const {
        return memory_mode_ ? mem_frame_size_ : reader_->frame_size();
    }
    int64_t next_file_frame(int64_t seq) const;
    void reader_loop();      // free-queue から取り出し、ファイル読み+CRC、filled-queue へ
    void scheduler_loop();   // filled-queue から取り出し、ring記録+ScheduleVideoFrame
    bool read_into(IDeckLinkMutableVideoFrame* frame, int64_t file_idx);
    bool schedule_one(const FilledItem& item);
    void fail(const std::string& msg);
    void release_output();
    void join_workers();

    IDeckLink* device_ = nullptr;
    IDeckLinkOutput* output_ = nullptr;
    std::unique_ptr<RawReader> reader_;
    // --- メモリソースモード(Phase 7): reader_ の代わりに mem_front_ から充填 ---
    // memory_mode_/mem_frame_size_ は start_impl(ワーカ起動前)で確定し再生中は不変。
    // stop() はワーカ join 後に mem_mutex_ 下でクリアする(並行 update_frame と排他)。
    std::atomic<bool> memory_mode_{false};
    int64_t mem_frame_size_ = 0;
    std::mutex mem_mutex_;                // mem_front_/mem_back_ 本体と swap を保護
    std::vector<uint8_t> mem_front_;      // reader_loop が読む現行フレーム
    std::vector<uint8_t> mem_back_;       // update_frame の書き込み先(書き込み後 swap)
    std::vector<IDeckLinkMutableVideoFrame*> pool_;  // 所有: stop()でのみRelease
    BMDTimeValue frame_duration_ = 0;
    BMDTimeScale time_scale_ = 0;
    bool loop_ = false;

    std::mutex queue_mutex_;
    std::condition_variable free_cv_;     // free_ に空きが出た / stop
    std::condition_variable filled_cv_;   // filled_ に要素が出た / stop
    std::deque<IDeckLinkMutableVideoFrame*> free_;   // 読み込み待ちの空きフレーム
    std::deque<FilledItem> filled_;                  // スケジュール待ちの充填済みフレーム
    std::atomic<bool> stop_workers_{false};
    std::thread reader_thread_;
    std::thread scheduler_thread_;

    mutable std::mutex error_mutex_;
    std::string error_;
    std::atomic<int64_t> state_{RDL_STATE_IDLE};
    std::atomic<int64_t> seq_{0};            // reader が払い出す通し番号(次に読むseq)
    std::atomic<int64_t> scheduled_{0};      // scheduler が ScheduleVideoFrame した数
    std::atomic<int64_t> current_frame_{0};
    std::atomic<int64_t> completed_{0};
    std::atomic<int64_t> dropped_{0};
    std::atomic<int64_t> late_{0};
    std::atomic<int64_t> applied_444_{-1};   // start()が同一configで読み戻したSDI 444(1/0)、-1=未設定
    std::atomic<int64_t> applied_link_{-1};  // 同 SDIリンク構成(bmdLinkConfiguration値)、-1=未設定
    std::atomic<int64_t> applied_hdr_{-1};   // メタ適用結果(-1=N/A,0=skip,1=適用)

    static constexpr int64_t kRing = 4096;
    std::mutex ring_mutex_;
    RdlFrameCrc ring_[kRing] = {};
    int64_t ring_write_ = 0;
    int64_t ring_read_ = 0;
};
