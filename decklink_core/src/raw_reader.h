// 流用元: 20260611_rawdecklink-signal-player/core/src/raw_reader.h （逐語コピー、改変なし）
#pragma once
#include <cstdint>
#include <string>

constexpr uint32_t RDL_FOURCC_R210 = 0x72323130u;  // 'r210' = bmdFormat10BitRGB
constexpr uint32_t RDL_FOURCC_V210 = 0x76323130u;  // 'v210' = bmdFormat10BitYUV

int64_t rdl_row_bytes(uint32_t fourcc, int64_t width);

#ifdef _WIN32
// UTF-8 <-> wide conversion (Windows API; device_enum/playback でも使用)
std::wstring utf8_to_wide(const char* utf8);
std::string wide_to_utf8(const wchar_t* wide);
#endif

// Frame-wise reader for headerless raw files. Errors: std::runtime_error.
// Windows: Win32 ReadFile 直叩き(ifstreamの内部バッファコピーを避けて
// 2160p59.94 r210 (~2GB/s) のフレーム読み込みに必要な帯域を確保する)。
// POSIX: pread(シーク状態を持たない)。パスは両OSともUTF-8で受ける
// (Windowsは内部でワイド変換)。
class RawReader {
public:
    RawReader(const std::string& utf8_path, int64_t width, int64_t height, uint32_t fourcc);
    ~RawReader();
    RawReader(const RawReader&) = delete;
    RawReader& operator=(const RawReader&) = delete;

    int64_t frame_size() const { return frame_size_; }
    int64_t frame_count() const { return frame_count_; }
    void read_frame(int64_t index, void* dst);

private:
#ifdef _WIN32
    void* handle_ = nullptr;  // HANDLE (windows.h を公開ヘッダに巻き込まないため void*)
#else
    int fd_ = -1;
#endif
    int64_t frame_size_ = 0;
    int64_t frame_count_ = 0;
};
