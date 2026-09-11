// hdr10_direct.cpp — Experiment A of docs/hdr-swapchain-adversarial-review-20260911.md (2026-09-11)
//
// Puts CPU-generated *integer* 10-bit PQ codes straight into an R10G10B10A2_UNORM flip-model swapchain
// with ID3D11DeviceContext::CopyResource. No shader, no render pass, no texture filtering, no float->UNORM
// conversion by the output merger: the bytes in the back buffer are the bytes this program computed.
// Before every N-th Present the back buffer is read back (staging copy) and compared byte for byte with
// the source image, and the ramp row is written to a CSV, so the codes that entered the display pipeline
// are proven, not assumed. If the DeckLink still shows the 49 two-code jumps, the application-side PQ
// encoding hypothesis is dead.
//
// The picture is the same layout as tools/proto_hdr_view.py (scaled to the output resolution):
//   rows  40..320 /1080 : 16 flat patches 2 .. 2000 nit (code = round(1023 * PQ(nit / 10000)))
//   rows 420..720 /1080 : left = 203 nit, right = 1000 nit
//   rows 800..1040/1080 : PQ-code-linear ramp, code(x) = round(x * 846 / (W - 1))  (846 = code of 2000 nit),
//                         every code 0..846 exactly once, 4 or 5 px wide at 3840 px (ROI 0,1800,3840,8 as before)
// --mode scrgb does the same with an R16G16B16A16_FLOAT swapchain (value = nit / 80 as IEEE half),
// as a control that the CopyResource path itself is sound (expected: 4/5 px steps, no skips).
//
// Build (MinGW-w64, like tools/dxgi_outputs.cpp):
//   g++ -std=c++17 -O2 tools/hdr10_direct.cpp -o tools/hdr10_direct.exe -ld3d11 -ldxgi -lole32 -luser32 -lgdi32
// Usage:
//   tools\hdr10_direct.exe --list
//   tools\hdr10_direct.exe --display 1 --mode hdr10 --seconds 60 --readback outputs\a_hdr10_backbuffer.csv
//   tools\hdr10_direct.exe --display 1 --mode hdr10 --windowed --seconds 3 --readback out.csv   (smoke test)
// ESC / Q closes the window. Run PresentMon concurrently (docs/PROCEDURE.md section 6); the process name
// to look for is hdr10_direct.exe. Exit code 0 = every readback matched the source, 3 = a mismatch was seen.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// ---------------------------------------------------------------- PQ (BT.2100-2 Table 4), same as tools/pq.py
static const double PQ_M1 = 2610.0 / 16384.0, PQ_M2 = 2523.0 / 4096.0 * 128.0;
static const double PQ_C1 = 3424.0 / 4096.0, PQ_C2 = 2413.0 / 4096.0 * 32.0, PQ_C3 = 2392.0 / 4096.0 * 32.0;

static double pq_oetf(double y) {            // normalised luminance (1.0 = 10000 nit) -> PQ signal 0..1
    if (y < 0) y = 0;
    if (y > 1) y = 1;
    double p = std::pow(y, PQ_M1);
    return std::pow((PQ_C1 + PQ_C2 * p) / (1.0 + PQ_C3 * p), PQ_M2);
}
static double pq_eotf(double e) {            // PQ signal 0..1 -> normalised luminance
    if (e < 0) e = 0;
    if (e > 1) e = 1;
    double p = std::pow(e, 1.0 / PQ_M2);
    double num = p - PQ_C1; if (num < 0) num = 0;
    return std::pow(num / (PQ_C2 - PQ_C3 * p), 1.0 / PQ_M1);
}
static int pq_code(double nit) { return (int)std::lround(1023.0 * pq_oetf(nit / 10000.0)); }  // round half away from zero

static uint16_t float_to_half(float f) {     // IEEE 754 binary16, round to nearest even
    uint32_t x; std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFF) == 0xFF) return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0));
    if (exp >= 0x1F) return (uint16_t)(sign | 0x7C00u);
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mant |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half = mant >> shift;
        uint32_t rem = mant & ((1u << shift) - 1), halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (half & 1))) half++;
        return (uint16_t)(sign | half);
    }
    uint32_t half = sign | ((uint32_t)exp << 10) | (mant >> 13);
    uint32_t rem = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1))) half++;
    return (uint16_t)half;
}
static float half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16, exp = (h >> 10) & 0x1F, mant = h & 0x3FFu, x;
    if (exp == 0) {
        if (mant == 0) x = sign;
        else { int e = -1; do { e++; mant <<= 1; } while (!(mant & 0x400u)); mant &= 0x3FFu;
               x = sign | ((uint32_t)(127 - 15 - e) << 23) | (mant << 13); }
    } else if (exp == 31) x = sign | 0x7F800000u | (mant << 13);
    else x = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    float f; std::memcpy(&f, &x, 4); return f;
}

// ---------------------------------------------------------------- pattern
static const double PATCH_NITS[16] = {2, 5, 10, 20, 40, 80, 120, 160, 203, 300, 400, 600, 800, 1000, 1500, 2000};
static const int RAMP_MAX_CODE = 846;        // pq_code(2000) = round(846.455)

struct Pattern {
    int w = 0, h = 0, rampY0 = 0, rampY1 = 0;
    std::vector<uint32_t> hdr10;             // R10G10B10A2_UNORM: R bits 0-9, G 10-19, B 20-29, A 30-31
    std::vector<uint16_t> fp16;              // R16G16B16A16_FLOAT, 4 halves per pixel
    std::vector<int> rampCode;               // expected code per x (hdr10) — for the CSV / self-check
};

static Pattern build_pattern(int w, int h, bool scrgb) {
    Pattern p; p.w = w; p.h = h;
    const double sx = w / 1920.0, sy = h / 1080.0;
    std::vector<double> nits((size_t)w * h, 0.0);
    std::vector<int> code((size_t)w * h, 0);
    auto fill = [&](int x0, int x1, int y0, int y1, double nit) {
        int c = pq_code(nit);
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { nits[(size_t)y * w + x] = nit; code[(size_t)y * w + x] = c; }
    };
    const int pw = w / 16;
    for (int i = 0; i < 16; ++i)
        fill(i * pw, (i + 1) * pw - (int)std::lround(4 * sx), (int)std::lround(40 * sy), (int)std::lround(320 * sy), PATCH_NITS[i]);
    fill((int)std::lround(40 * sx), w / 2 - (int)std::lround(20 * sx), (int)std::lround(420 * sy), (int)std::lround(720 * sy), 203.0);
    fill(w / 2 + (int)std::lround(20 * sx), w - (int)std::lround(40 * sx), (int)std::lround(420 * sy), (int)std::lround(720 * sy), 1000.0);
    p.rampY0 = (int)std::lround(800 * sy); p.rampY1 = (int)std::lround(1040 * sy);
    p.rampCode.resize(w);
    for (int x = 0; x < w; ++x) {
        int c = (int)std::lround((double)x * RAMP_MAX_CODE / (w - 1));
        p.rampCode[x] = c;
        double nit = pq_eotf(c / 1023.0) * 10000.0;
        for (int y = p.rampY0; y < p.rampY1; ++y) { code[(size_t)y * w + x] = c; nits[(size_t)y * w + x] = nit; }
    }
    if (scrgb) {
        p.fp16.resize((size_t)w * h * 4);
        const uint16_t one = float_to_half(1.0f);
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            uint16_t v = float_to_half((float)(nits[i] / 80.0));
            p.fp16[i * 4 + 0] = p.fp16[i * 4 + 1] = p.fp16[i * 4 + 2] = v; p.fp16[i * 4 + 3] = one;
        }
    } else {
        p.hdr10.resize((size_t)w * h);
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            uint32_t c = (uint32_t)code[i];
            p.hdr10[i] = c | (c << 10) | (c << 20) | (3u << 30);
        }
    }
    return p;
}

// ---------------------------------------------------------------- helpers
#define CHECK(hr, what) do { HRESULT _h = (hr); if (FAILED(_h)) { fprintf(stderr, "FAILED %s: 0x%08lX\n", what, (unsigned long)_h); return 1; } } while (0)

template <class T> struct Com { T* p = nullptr; ~Com() { if (p) p->Release(); } T* operator->() { return p; } T** put() { return &p; } };

struct DisplayEntry { UINT adapter, output; std::wstring adapterName, deviceName; RECT rect; int colorSpace; UINT bits; };

static std::vector<DisplayEntry> enumerate(IDXGIFactory6* f) {
    std::vector<DisplayEntry> v;
    for (UINT i = 0;; ++i) {
        Com<IDXGIAdapter1> a; if (f->EnumAdapters1(i, a.put()) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 ad{}; a->GetDesc1(&ad);
        for (UINT j = 0;; ++j) {
            Com<IDXGIOutput> o; if (a->EnumOutputs(j, o.put()) == DXGI_ERROR_NOT_FOUND) break;
            Com<IDXGIOutput6> o6; DisplayEntry e{}; e.adapter = i; e.output = j; e.adapterName = ad.Description;
            if (SUCCEEDED(o->QueryInterface(__uuidof(IDXGIOutput6), (void**)o6.put()))) {
                DXGI_OUTPUT_DESC1 d{}; o6->GetDesc1(&d);
                e.deviceName = d.DeviceName; e.rect = d.DesktopCoordinates; e.colorSpace = d.ColorSpace; e.bits = d.BitsPerColor;
            } else { DXGI_OUTPUT_DESC d{}; o->GetDesc(&d); e.deviceName = d.DeviceName; e.rect = d.DesktopCoordinates; e.colorSpace = -1; e.bits = 0; }
            v.push_back(e);
        }
    }
    return v;
}

static bool g_close = false;
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN: if (w == VK_ESCAPE || w == 'Q') g_close = true; return 0;
    case WM_CLOSE: g_close = true; return 0;
    case WM_SETCURSOR: SetCursor(nullptr); return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
}

static int arg_int(int argc, char** argv, const char* name, int def) {
    for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], name)) return atoi(argv[i + 1]);
    return def;
}
static const char* arg_str(int argc, char** argv, const char* name, const char* def) {
    for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], name)) return argv[i + 1];
    return def;
}
static bool arg_flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], name)) return true;
    return false;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Per-monitor DPI awareness so that output rectangles and window sizes are physical pixels (Win10 1703+).
    if (auto u32 = GetModuleHandleW(L"user32.dll")) {
        typedef BOOL(WINAPI * Fn)(HANDLE);
        if (auto fn = (Fn)GetProcAddress(u32, "SetProcessDpiAwarenessContext")) fn((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */);
    }
    Com<IDXGIFactory6> factory;
    CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory6), (void**)factory.put()), "CreateDXGIFactory1");
    auto displays = enumerate(factory.p);

    if (arg_flag(argc, argv, "--list") || argc == 1) {
        for (size_t k = 0; k < displays.size(); ++k) {
            const auto& e = displays[k];
            wprintf(L"[%zu] adapter %u (%ls) output %u %ls  rect=(%ld,%ld)-(%ld,%ld) %ldx%ld  bits=%u  ColorSpace=%d%ls\n",
                    k, e.adapter, e.adapterName.c_str(), e.output, e.deviceName.c_str(),
                    e.rect.left, e.rect.top, e.rect.right, e.rect.bottom, e.rect.right - e.rect.left, e.rect.bottom - e.rect.top,
                    e.bits, e.colorSpace, e.colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? L" (PQ/2020 = HDR)" : L"");
        }
        if (argc == 1) printf("usage: hdr10_direct --display N [--mode hdr10|scrgb] [--windowed] [--seconds S] [--readback file.csv]"
                              " [--readback-every N] [--metadata]\n");
        return 0;
    }

    const int disp = arg_int(argc, argv, "--display", -1);
    if (disp < 0 || disp >= (int)displays.size()) { fprintf(stderr, "--display N required (see --list)\n"); return 2; }
    const bool scrgb = !strcmp(arg_str(argc, argv, "--mode", "hdr10"), "scrgb");
    const bool windowed = arg_flag(argc, argv, "--windowed");
    const double seconds = atof(arg_str(argc, argv, "--seconds", "0"));
    const char* readbackPath = arg_str(argc, argv, "--readback", nullptr);
    const int readbackEvery = arg_int(argc, argv, "--readback-every", 24);
    const bool metadata = arg_flag(argc, argv, "--metadata");
    const DisplayEntry& D = displays[disp];

    // ---- device on the adapter that owns the output (hybrid-GPU laptops: never let D3D pick)
    Com<IDXGIAdapter1> adapter; CHECK(factory->EnumAdapters1(D.adapter, adapter.put()), "EnumAdapters1");
    Com<ID3D11Device> dev; Com<ID3D11DeviceContext> ctx; D3D_FEATURE_LEVEL fl{};
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    CHECK(D3D11CreateDevice(adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, want, 2, D3D11_SDK_VERSION, dev.put(), &fl, ctx.put()),
          "D3D11CreateDevice");
    wprintf(L"device: adapter %u %ls  feature level 0x%X\n", D.adapter, D.adapterName.c_str(), (unsigned)fl);

    // ---- window exactly covering the output (borderless popup) or a small overlapped window
    WNDCLASSW wc{}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"hdr10_direct";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);
    RECT r = D.rect;
    HWND hwnd;
    if (windowed) {
        RECT wr{r.left + 80, r.top + 80, r.left + 80 + 960, r.top + 80 + 540};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExW(0, wc.lpszClassName, scrgb ? L"hdr10_direct — scrgb" : L"hdr10_direct — hdr10", WS_OVERLAPPEDWINDOW,
                               wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, wc.hInstance, nullptr);
    } else {
        hwnd = CreateWindowExW(0, wc.lpszClassName, scrgb ? L"hdr10_direct — scrgb" : L"hdr10_direct — hdr10", WS_POPUP,
                               r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    }
    if (!hwnd) { fprintf(stderr, "CreateWindow failed\n"); return 1; }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    RECT cr{}; GetClientRect(hwnd, &cr);
    const int W = cr.right - cr.left, H = cr.bottom - cr.top;
    wprintf(L"window: hwnd=%p client %dx%d on %ls (%ld,%ld)-(%ld,%ld) %ls\n", (void*)hwnd, W, H, D.deviceName.c_str(),
            r.left, r.top, r.right, r.bottom, windowed ? L"[windowed]" : L"[covers the output]");

    // ---- flip-model swapchain
    const DXGI_FORMAT fmt = scrgb ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R10G10B10A2_UNORM;
    const DXGI_COLOR_SPACE_TYPE cs = scrgb ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = W; sd.Height = H; sd.Format = fmt; sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Scaling = DXGI_SCALING_NONE; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    Com<IDXGISwapChain1> sc1;
    CHECK(factory->CreateSwapChainForHwnd(dev.p, hwnd, &sd, nullptr, nullptr, sc1.put()), "CreateSwapChainForHwnd");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    Com<IDXGISwapChain4> sc;
    CHECK(sc1->QueryInterface(__uuidof(IDXGISwapChain4), (void**)sc.put()), "QueryInterface IDXGISwapChain4");
    UINT csFlags = 0;
    CHECK(sc->CheckColorSpaceSupport(cs, &csFlags), "CheckColorSpaceSupport");
    printf("swapchain: %s %dx%d FLIP_DISCARD x2, color space %d support flags=0x%X (PRESENT=%d OVERLAY_PRESENT=%d)\n",
           scrgb ? "R16G16B16A16_FLOAT" : "R10G10B10A2_UNORM", W, H, (int)cs, csFlags,
           !!(csFlags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT), !!(csFlags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_OVERLAY_PRESENT));
    if (!(csFlags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) fprintf(stderr, "WARNING: color space not supported for present on this output (HDR off?)\n");
    CHECK(sc->SetColorSpace1(cs), "SetColorSpace1");
    if (metadata) {
        DXGI_HDR_METADATA_HDR10 md{};
        md.RedPrimary[0] = 34000; md.RedPrimary[1] = 16000; md.GreenPrimary[0] = 13250; md.GreenPrimary[1] = 34500;
        md.BluePrimary[0] = 7500; md.BluePrimary[1] = 3000; md.WhitePoint[0] = 15635; md.WhitePoint[1] = 16450;
        md.MaxMasteringLuminance = 1000 * 10000; md.MinMasteringLuminance = 50; md.MaxContentLightLevel = 2000; md.MaxFrameAverageLightLevel = 400;
        CHECK(sc->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(md), &md), "SetHDRMetaData");
        printf("HDR metadata: BT.2020 / D65, mastering 0.005..1000 nit, MaxCLL 2000, MaxFALL 400\n");
    }

    // ---- source image (DEFAULT texture, initialised from CPU memory) + staging texture for readback
    Pattern pat = build_pattern(W, H, scrgb);
    const UINT pitch = (UINT)W * (scrgb ? 8 : 4);
    const void* srcBytes = scrgb ? (const void*)pat.fp16.data() : (const void*)pat.hdr10.data();
    D3D11_TEXTURE2D_DESC td{}; td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = srcBytes; init.SysMemPitch = pitch;
    Com<ID3D11Texture2D> src; CHECK(dev->CreateTexture2D(&td, &init, src.put()), "CreateTexture2D(source)");
    D3D11_TEXTURE2D_DESC st = td; st.Usage = D3D11_USAGE_STAGING; st.BindFlags = 0; st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> staging; CHECK(dev->CreateTexture2D(&st, nullptr, staging.put()), "CreateTexture2D(staging)");
    printf("pattern: %dx%d, ramp rows %d..%d, ramp codes 0..%d (%d px per code)\n", W, H, pat.rampY0, pat.rampY1 - 1, RAMP_MAX_CODE,
           W / (RAMP_MAX_CODE + 1));
    printf("patch codes:"); for (double n : PATCH_NITS) printf(" %g:%d", n, pq_code(n)); printf("\n");

    FILE* csv = nullptr;
    if (readbackPath) { csv = fopen(readbackPath, "w"); if (!csv) { fprintf(stderr, "cannot open %s\n", readbackPath); return 1; } }
    const int rampRow = (pat.rampY0 + pat.rampY1) / 2;

    // ---- loop: CopyResource(back buffer <- source) [readback] Present(1)
    ULONGLONG t0 = GetTickCount64();
    long presents = 0, readbacks = 0, mismatches = 0;
    while (!g_close) {
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (g_close) break;
        Com<ID3D11Texture2D> bb; CHECK(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)bb.put()), "GetBuffer");
        ctx->CopyResource(bb.p, src.p);
        if (readbackEvery > 0 && presents % readbackEvery == 0) {
            ctx->CopyResource(staging.p, bb.p);
            D3D11_MAPPED_SUBRESOURCE map{};
            CHECK(ctx->Map(staging.p, 0, D3D11_MAP_READ, 0, &map), "Map(staging)");
            long bad = 0;
            for (int y = 0; y < H; ++y)
                if (std::memcmp((const char*)map.pData + (size_t)y * map.RowPitch, (const char*)srcBytes + (size_t)y * pitch, pitch)) bad++;
            readbacks++; if (bad) mismatches++;
            if (readbacks == 1 || bad) printf("readback #%ld (present %ld): back buffer vs source — %ld of %d rows differ %s\n",
                                             readbacks, presents, bad, H, bad ? "MISMATCH" : "(bit-identical)");
            if (csv && readbacks == 1) {
                const char* row = (const char*)map.pData + (size_t)rampRow * map.RowPitch;
                fprintf(csv, "x,R,G,B,expected_code\n");
                for (int x = 0; x < W; ++x) {
                    if (scrgb) { const uint16_t* h4 = (const uint16_t*)row + (size_t)x * 4;
                        fprintf(csv, "%d,%.8g,%.8g,%.8g,%d\n", x, half_to_float(h4[0]), half_to_float(h4[1]), half_to_float(h4[2]), pat.rampCode[x]); }
                    else { uint32_t v = ((const uint32_t*)row)[x];
                        fprintf(csv, "%d,%u,%u,%u,%d\n", x, v & 0x3FF, (v >> 10) & 0x3FF, (v >> 20) & 0x3FF, pat.rampCode[x]); }
                }
                fflush(csv);
                printf("readback CSV: %s (back-buffer row %d, %d px)\n", readbackPath, rampRow, W);
            }
            ctx->Unmap(staging.p, 0);
        }
        HRESULT hr = sc->Present(1, 0);
        if (FAILED(hr)) { fprintf(stderr, "Present failed 0x%08lX\n", (unsigned long)hr); break; }
        presents++;
        if (seconds > 0 && GetTickCount64() - t0 >= (ULONGLONG)(seconds * 1000.0)) g_close = true;
    }
    if (csv) fclose(csv);
    double dt = (GetTickCount64() - t0) / 1000.0;
    printf("done: %ld presents in %.1f s (%.1f/s), %ld readbacks, %ld with mismatches\n", presents, dt, presents / (dt > 0 ? dt : 1), readbacks, mismatches);
    DestroyWindow(hwnd);
    return mismatches ? 3 : 0;
}
