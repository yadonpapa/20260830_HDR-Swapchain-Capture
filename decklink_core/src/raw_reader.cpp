// 流用元: 20260611_rawdecklink-signal-player/core/src/raw_reader.cpp （逐語コピー、改変なし）
#include "raw_reader.h"

#include <stdexcept>

int64_t rdl_row_bytes(uint32_t fourcc, int64_t width) {
    if (fourcc == RDL_FOURCC_R210) return ((width + 63) / 64) * 256;
    if (fourcc == RDL_FOURCC_V210) return ((width + 47) / 48) * 128;
    throw std::runtime_error("unsupported pixel format fourcc");
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::wstring utf8_to_wide(const char* utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
    return w;
}

std::string wide_to_utf8(const wchar_t* wide) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wide, -1, s.data(), n, nullptr, nullptr);
    return s;
}

RawReader::RawReader(const std::string& utf8_path, int64_t width, int64_t height,
                     uint32_t fourcc) {
    frame_size_ = rdl_row_bytes(fourcc, width) * height;
    std::wstring path = utf8_to_wide(utf8_path.c_str());
    // FILE_FLAG_SEQUENTIAL_SCAN: シーケンシャル read-ahead を有効化(ループ再生に有利)。
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open raw file");
    handle_ = h;
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li)) {
        CloseHandle(h);
        handle_ = nullptr;
        throw std::runtime_error("cannot stat raw file");
    }
    int64_t size = li.QuadPart;
    if (size <= 0 || size % frame_size_ != 0) {
        CloseHandle(h);
        handle_ = nullptr;
        throw std::runtime_error(
            "raw file size " + std::to_string(size) +
            " is not a positive multiple of frame size " + std::to_string(frame_size_));
    }
    frame_count_ = size / frame_size_;
}

RawReader::~RawReader() {
    if (handle_) CloseHandle((HANDLE)handle_);
}

void RawReader::read_frame(int64_t index, void* dst) {
    if (index < 0 || index >= frame_count_) throw std::runtime_error("frame index out of range");
    LARGE_INTEGER pos;
    pos.QuadPart = index * frame_size_;
    if (!SetFilePointerEx((HANDLE)handle_, pos, nullptr, FILE_BEGIN))
        throw std::runtime_error("raw file seek error");
    char* out = static_cast<char*>(dst);
    int64_t remaining = frame_size_;
    while (remaining > 0) {
        // ReadFile は1回で最大 ~4GB-1 だが念のためチャンク分割し全量読み切る。
        DWORD want = remaining > 0x40000000 ? 0x40000000u : (DWORD)remaining;
        DWORD got = 0;
        if (!ReadFile((HANDLE)handle_, out, want, &got, nullptr) || got == 0)
            throw std::runtime_error("raw file read error");
        out += got;
        remaining -= got;
    }
}

#else  // POSIX (macOS / Linux)

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

RawReader::RawReader(const std::string& utf8_path, int64_t width, int64_t height,
                     uint32_t fourcc) {
    frame_size_ = rdl_row_bytes(fourcc, width) * height;
    int fd = ::open(utf8_path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open raw file");
#ifdef __APPLE__
    ::fcntl(fd, F_RDAHEAD, 1);  // シーケンシャル read-ahead(FILE_FLAG_SEQUENTIAL_SCAN相当)
#endif
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("cannot stat raw file");
    }
    int64_t size = (int64_t)st.st_size;
    if (size <= 0 || size % frame_size_ != 0) {
        ::close(fd);
        throw std::runtime_error(
            "raw file size " + std::to_string(size) +
            " is not a positive multiple of frame size " + std::to_string(frame_size_));
    }
    fd_ = fd;
    frame_count_ = size / frame_size_;
}

RawReader::~RawReader() {
    if (fd_ >= 0) ::close(fd_);
}

void RawReader::read_frame(int64_t index, void* dst) {
    if (index < 0 || index >= frame_count_) throw std::runtime_error("frame index out of range");
    char* out = static_cast<char*>(dst);
    int64_t remaining = frame_size_;
    off_t pos = (off_t)(index * frame_size_);
    while (remaining > 0) {
        ssize_t got = ::pread(fd_, out, (size_t)remaining, pos);
        if (got <= 0) throw std::runtime_error("raw file read error");
        out += got;
        pos += got;
        remaining -= got;
    }
}

#endif
