// 流用元: 20260611_rawdecklink-signal-player/core/src/rdl_api.h
// 改変点(Phase 7 Task 2): rdl_start_playback_memory / rdl_update_frame を追加
//   (エクスポート関数の新設のみ。既存の構造体・関数シグネチャ・ABI は不変)。
// core/src/rdl_api.h — 公開C ABI。Python ctypes定義と1対1対応(spec §7)。
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define RDL_EXPORT __declspec(dllexport)
#else
#define RDL_EXPORT __attribute__((visibility("default")))
#endif

/* エラーコード */
#define RDL_OK             0
#define RDL_E_INIT        -1
#define RDL_E_BADARG      -2
#define RDL_E_NODEVICE    -3
#define RDL_E_UNSUPPORTED -4
#define RDL_E_FILE        -5
#define RDL_E_BUSY        -6
#define RDL_E_INTERNAL    -7
#define RDL_E_NOSIGNAL    -8   /* 入力信号がロックしない/タイムアウト */

/* 再生状態 */
#define RDL_STATE_IDLE    0
#define RDL_STATE_PLAYING 1
#define RDL_STATE_ERROR   2
#define RDL_STATE_STOPPED 3

#pragma pack(push, 8)
typedef struct {            /* sizeof == 272 */
    char name[256];         /* UTF-8 */
    int64_t has_sdi_output;
    int64_t has_hdmi_output;
} RdlDeviceInfo;

typedef struct {            /* sizeof == 120 */
    int64_t mode_id;        /* BMDDisplayMode */
    char name[64];          /* 例 "1080p23.98" */
    int64_t width, height;
    int64_t fps_num, fps_den;   /* timeScale / frameDuration */
    int64_t supports_r210, supports_v210;
} RdlModeInfo;

typedef struct {            /* sizeof == 1176 */
    int64_t mode_id;
    int64_t pixel_format;   /* fourcc 'r210'=0x72323130 / 'v210'=0x76323130 */
    char raw_path_utf8[1024];
    int64_t loop_playback;  /* 0/1 */
    int64_t preroll_frames; /* 0 → 既定8(最小3にクランプ) */
    /* --- HDR/色域メタデータ(SDI VPID用)。hdr_present=0 なら未設定で従来通り --- */
    int64_t hdr_present;    /* 0=メタ付与しない / 1=付与 */
    int64_t colorspace;    /* BMDColorspace FourCC (0=未設定) */
    int64_t eotf;          /* CEA 861.3 EOTF 0-7 */
    double prim_red_x, prim_red_y, prim_green_x, prim_green_y;
    double prim_blue_x, prim_blue_y, white_x, white_y;
    double max_dml, min_dml, max_cll, max_fall;
} RdlPlaybackConfig;

typedef struct {            /* sizeof == 80 */
    int64_t state;
    int64_t file_frame_count;
    int64_t current_frame;
    int64_t completed_frames;
    int64_t dropped_frames;
    int64_t late_frames;
    int64_t scheduled_frames;
    int64_t sdi_444;        /* 1=RGB444 / 0=YCbCr422 / -1=未設定 */
    int64_t sdi_link;       /* bmdLinkConfiguration値(Dual=0x6c63646c) / -1 */
    int64_t hdr_applied;    /* -1=N/A / 0=skip(非対応含む) / 1=適用 */
} RdlStatus;

typedef struct {            /* sizeof == 24 */
    int64_t sequence;       /* スケジュール通し番号 */
    int64_t file_frame;     /* ファイル内フレーム番号 */
    int64_t crc32;          /* 下位32bitが有効 */
} RdlFrameCrc;

typedef struct {            /* sizeof == 32 */
    int64_t sdi_444;        /* 1=RGB444 / 0=YCbCr422 */
    int64_t sdi_link;       /* bmdLinkConfiguration値 */
    int64_t level_a;        /* 1=Level A / 0=Level B */
    int64_t default_mode;   /* bmdDeckLinkConfigDefaultVideoOutputMode(BMDDisplayMode) */
} RdlSdiSettings;

typedef struct {            /* sizeof == 80 */
    int64_t current_profile;   /* 現在の bmdProfileID。無ければ0 */
    int64_t count;             /* profiles[] の有効数(最大8) */
    int64_t profiles[8];       /* 利用可能 bmdProfileID 列 */
} RdlProfileList;

typedef struct {            /* sizeof == 160 */
    int64_t mode_id;            /* 検出された BMDDisplayMode */
    char    mode_name[64];      /* 例 "1080p30"（UTF-8） */
    int64_t width, height;
    int64_t row_bytes;
    int64_t pixel_format;       /* fourcc 'r210' / 'v210' */
    int64_t detected_flags;     /* BMDDetectedVideoInputFormatFlags */
    int64_t hdr_present;        /* 0/1（bmdFrameContainsHDRMetadata） */
    int64_t eotf;               /* CEA 861.3 EOTF。未取得は -1 */
    double  max_cll, max_fall, max_dml, min_dml;   /* 未取得は -1.0 */
} RdlCapturedInfo;
#pragma pack(pop)

typedef void* RdlHandle;

/* 初期化・列挙 */
RDL_EXPORT int rdl_initialize(void);
RDL_EXPORT void rdl_shutdown(void);
RDL_EXPORT int rdl_get_device_count(void);
/* デバイス増減(ホットプラグ)通知の世代カウンタ。到着/除去のたびに増える。
 * rdl_shutdown / rdl_initialize をまたいで単調増加。未初期化でも安全に呼べる。 */
RDL_EXPORT int rdl_get_device_generation(void);
RDL_EXPORT int rdl_get_device_info(int device_index, RdlDeviceInfo* out);
RDL_EXPORT int rdl_get_mode_count(int device_index);
RDL_EXPORT int rdl_get_mode_info(int device_index, int mode_index, RdlModeInfo* out);
RDL_EXPORT int rdl_get_last_error_global(char* buf, int len);

/* SDI出力設定を読む(検証/メニュー表示)。device_index で問い合わせ。 */
RDL_EXPORT int rdl_get_sdi_settings(int device_index, RdlSdiSettings* out);
/* SDI出力設定を書き、WriteConfigurationToPreferences で恒久保存する。 */
RDL_EXPORT int rdl_set_sdi_settings(int device_index, const RdlSdiSettings* s);
/* コネクタプロファイルを列挙(現在値と利用可能一覧)。 */
RDL_EXPORT int rdl_get_connector_profiles(int device_index, RdlProfileList* out);
/* コネクタプロファイルを切替(SetActive)。切替後は呼び出し側で再列挙すること。 */
RDL_EXPORT int rdl_set_connector_profile(int device_index, int64_t profile_id);

/* 再生(実装はTask 12) */
RDL_EXPORT RdlHandle rdl_open(int device_index);
RDL_EXPORT int rdl_start_playback(RdlHandle h, const RdlPlaybackConfig* cfg);
RDL_EXPORT int rdl_stop(RdlHandle h);
RDL_EXPORT void rdl_close(RdlHandle h);
RDL_EXPORT int rdl_get_status(RdlHandle h, RdlStatus* out);
RDL_EXPORT int rdl_get_frame_crcs(RdlHandle h, RdlFrameCrc* buf, int max_count);
RDL_EXPORT int rdl_get_last_error(RdlHandle h, char* buf, int len);

/* メモリバッファ再生(Phase 7): cfg->raw_path_utf8 は無視。frame_bytes(size バイト、
 * r210/v210 の 1 フレーム分)をエンジン内部へコピーし、1 フレームの
 * ループ再生として開始する。size はモード・pixel_format から計算される
 * フレームサイズと一致しなければ RDL_E_BADARG。 */
RDL_EXPORT int rdl_start_playback_memory(RdlHandle h, const RdlPlaybackConfig* cfg,
                                         const void* frame_bytes, int64_t size);
/* 再生を止めずにフレーム内容を差し替える(RDL_STATE_PLAYING 中のみ)。
 * 内部ダブルバッファへコピーし、以降に充填されるフレームから反映される
 * (反映レイテンシはプール/プリロール分 ≦ 約 8 フレーム)。 */
RDL_EXPORT int rdl_update_frame(RdlHandle h, const void* frame_bytes, int64_t size);

/* ABI検証: struct_id 1..5 = DeviceInfo/ModeInfo/PlaybackConfig/Status/FrameCrc,
 * 6 = RdlSdiSettings, 7 = RdlProfileList, 8 = RdlCapturedInfo のsizeof */
RDL_EXPORT int rdl_sizeof(int struct_id);

/* --- HDMI ループバック取り込み(2026-07-26 設計書)。既存 API は不変 --- */
typedef void* RdlInHandle;

/* デバイスの入力コネクタマスク(BMDVideoConnection の OR)。入力非対応なら 0 */
RDL_EXPORT int rdl_input_get_connections(int device_index, int64_t* out_mask);
/* connection = bmdVideoConnectionHDMI(2) 等。IDeckLinkConfiguration は
 * close までハンドルが保持する(解放すると設定が戻るため — 設計書 §4.2) */
RDL_EXPORT RdlInHandle rdl_input_open(int device_index, int64_t connection);
RDL_EXPORT int rdl_input_start(RdlInHandle h);   /* フォーマット自動検出付き */
/* skip_frames 枚捨てて 1 枚を buf へコピー。timeout_ms 超過は RDL_E_NOSIGNAL。
 * buf が小さければ RDL_E_BADARG(必要サイズは out->row_bytes * out->height) */
RDL_EXPORT int rdl_input_grab(RdlInHandle h, int skip_frames, int timeout_ms,
                              void* buf, int64_t size, RdlCapturedInfo* out);
RDL_EXPORT int rdl_input_stop(RdlInHandle h);
RDL_EXPORT void rdl_input_close(RdlInHandle h);
RDL_EXPORT int rdl_input_get_last_error(RdlInHandle h, char* buf, int len);

#ifdef __cplusplus
}
#endif
