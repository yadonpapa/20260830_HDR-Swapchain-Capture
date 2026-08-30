// 流用元: 20260611_rawdecklink-signal-player/core/src/platform_compat.h （逐語コピー、改変なし）
// core/src/platform_compat.h — DeckLink API のOS差吸収(BMD SDKサンプルのplatform.h方式)
// Windows: MIDL生成ヘッダ + COM(BSTR/BOOL)。macOS: DeckLinkAPI.h + CoreFoundation。
#pragma once
#include <cstring>
#include <string>

#ifdef _WIN32
  #include <windows.h>
  #include "DeckLinkAPI_h.h"
  using dlbool_t = BOOL;
  using dlstring_t = BSTR;
  std::string wide_to_utf8(const wchar_t* wide);  // raw_reader.cpp
  inline std::string dlstring_to_utf8(dlstring_t s) {
      return s ? wide_to_utf8(s) : std::string();
  }
  inline void dlstring_free(dlstring_t s) { if (s) SysFreeString(s); }
  inline bool rdl_iid_equal(REFIID a, REFIID b) {
      return memcmp(&a, &b, sizeof(IID)) == 0;
  }
  #define RDL_IID_IUNKNOWN IID_IUnknown
#else
  #include <CoreFoundation/CoreFoundation.h>
  #include "DeckLinkAPI.h"
  using dlbool_t = bool;
  using dlstring_t = CFStringRef;
  inline std::string dlstring_to_utf8(dlstring_t s) {
      if (!s) return {};
      CFIndex len = CFStringGetLength(s);
      CFIndex cap = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
      std::string out((size_t)cap, '\0');
      if (!CFStringGetCString(s, out.data(), cap, kCFStringEncodingUTF8)) return {};
      out.resize(std::strlen(out.c_str()));
      return out;
  }
  inline void dlstring_free(dlstring_t s) { if (s) CFRelease(s); }
  inline bool rdl_iid_equal(REFIID a, REFIID b) {
      return memcmp(&a, &b, sizeof(REFIID)) == 0;
  }
  #define RDL_IID_IUNKNOWN CFUUIDGetUUIDBytes(IUnknownUUID)
#endif
