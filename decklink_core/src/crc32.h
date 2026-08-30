// 流用元: 20260611_rawdecklink-signal-player/core/src/crc32.h （逐語コピー、改変なし）
#pragma once
#include <cstddef>
#include <cstdint>

// CRC-32/ISO-HDLC (poly 0xEDB88320, zlib.crc32-compatible). Pass prev crc for incremental.
uint32_t rdl_crc32(uint32_t crc, const void* data, size_t len);
