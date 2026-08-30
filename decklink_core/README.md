# decklink_core — DeckLink capture/playback helper DLL

C++ wrapper around the Blackmagic DeckLink SDK exposing a small C API (`rdl_api.h`).
`tools/dither_capture.py` uses only the input side (`rdl_input_open / start / grab / stop / close`):
format auto-detection, and 10-bit RGB (`r210`) frames when the HDMI input is RGB 4:4:4.

The **DeckLink SDK is not redistributable** and is not included. Download it from
<https://www.blackmagicdesign.com/support> (tested with SDK 16.0) and install Desktop Video.

## Build (Windows, MSVC)

```powershell
# from a "x64 Native Tools Command Prompt" (midl.exe must be on PATH)
cmake -S decklink_core -B decklink_core/build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DDECKLINK_SDK_DIR="C:/Blackmagic DeckLink SDK 16.0"
cmake --build decklink_core/build
copy decklink_core\build\rawdecklink_core.dll bin\
```

`tools/dither_capture.py` looks for the DLL in `bin/`, `decklink_core/build/`, next to itself, or at
`$RDL_CORE_DLL`.

Origin: extracted from the author's LUT-Adapt project (same license as this repository).
