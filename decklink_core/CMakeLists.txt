# core/CMakeLists.txt — Windows(MSVC/MIDL) と macOS(AppleClang/Dispatch) の両対応
cmake_minimum_required(VERSION 3.21)
project(rawdecklink_core C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ソースはUTF-8(BOMなし)。システムコードページに依存させない
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

if(WIN32)
  set(DECKLINK_SDK_DIR "C:/Blackmagic DeckLink SDK 16.0" CACHE PATH "DeckLink SDK root")
  set(DL_INCLUDE "${DECKLINK_SDK_DIR}/Win/include")
  if(NOT EXISTS "${DL_INCLUDE}/DeckLinkAPI.idl")
    message(FATAL_ERROR "DeckLink SDK not found: ${DL_INCLUDE} — -DDECKLINK_SDK_DIR=<path> を指定")
  endif()
  set(DL_GEN "${CMAKE_BINARY_DIR}/decklink_gen")
  file(MAKE_DIRECTORY ${DL_GEN})
  # DeckLinkAPI.idl → DeckLinkAPI_h.h / DeckLinkAPI_i.c (MIDLはvcvars環境のPATHにある)
  add_custom_command(
    OUTPUT ${DL_GEN}/DeckLinkAPI_h.h ${DL_GEN}/DeckLinkAPI_i.c
    COMMAND midl /nologo /env x64 /I "${DL_INCLUDE}"
            /h DeckLinkAPI_h.h /iid DeckLinkAPI_i.c /out "${DL_GEN}"
            "${DL_INCLUDE}/DeckLinkAPI.idl"
    DEPENDS "${DL_INCLUDE}/DeckLinkAPI.idl"
    COMMENT "MIDL: DeckLinkAPI.idl")
  # 生成ヘッダを include する全 .cpp のコンパイルより先に MIDL を完了させる。
  # custom command の消費者が DeckLinkAPI_i.c だけだと、クリーン並列ビルドで
  # 「DeckLinkAPI_h.h: No such file」が不定に出る。
  add_custom_target(decklink_midl_gen
    DEPENDS ${DL_GEN}/DeckLinkAPI_h.h ${DL_GEN}/DeckLinkAPI_i.c)
  set(DL_EXTRA_SOURCES ${DL_GEN}/DeckLinkAPI_i.c)
  set(DL_EXTRA_INCLUDE ${DL_GEN})
elseif(APPLE)
  set(DECKLINK_SDK_DIR "/Applications/Blackmagic DeckLink SDK 16.0" CACHE PATH "DeckLink SDK root")
  set(DL_INCLUDE "${DECKLINK_SDK_DIR}/Mac/include")
  if(NOT EXISTS "${DL_INCLUDE}/DeckLinkAPI.h")
    message(FATAL_ERROR "DeckLink SDK not found: ${DL_INCLUDE} — -DDECKLINK_SDK_DIR=<path> を指定")
  endif()
  # macOSはIDL生成不要。SDK同梱のDispatch(実行時にDeckLinkAPI.frameworkを動的ロード)を同梱ビルド。
  set(DL_EXTRA_SOURCES "${DL_INCLUDE}/DeckLinkAPIDispatch.cpp")
  set(DL_EXTRA_INCLUDE "")
else()
  message(FATAL_ERROR "unsupported platform (Windows / macOS のみ)")
endif()

add_library(rawdecklink_core SHARED
  src/crc32.cpp
  src/raw_reader.cpp
  src/device_enum.cpp
  src/rdl_api.cpp
  src/playback.cpp
  src/capture.cpp
  ${DL_EXTRA_SOURCES})
target_include_directories(rawdecklink_core PRIVATE src ${DL_EXTRA_INCLUDE} "${DL_INCLUDE}")
if(WIN32)
  add_dependencies(rawdecklink_core decklink_midl_gen)
  target_link_libraries(rawdecklink_core PRIVATE ole32 oleaut32)
  target_compile_definitions(rawdecklink_core PRIVATE UNICODE _UNICODE NOMINMAX)
elseif(APPLE)
  target_link_libraries(rawdecklink_core PRIVATE "-framework CoreFoundation")
endif()

add_executable(core_selftest
  selftest/selftest_main.cpp
  src/crc32.cpp
  src/raw_reader.cpp)
target_include_directories(core_selftest PRIVATE src)
