// DXGI の全アダプタ／出力を列挙し、各出力の DXGI_OUTPUT_DESC1（ColorSpace・bit 深度・輝度）を表示する
// 診断ツール（2026-08-30）。Qt の HDR スワップチェーン判定（QRhi D3D）はこの ColorSpace が
// DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020(12) かどうかで決まる。
//   g++ -std=c++17 -O1 tools/dxgi_outputs.cpp -o tools/dxgi_outputs.exe -ldxgi -lole32
#include <windows.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cwchar>

int main() {
    IDXGIFactory6* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory6), (void**)&f))) {
        printf("CreateDXGIFactory1 failed\n");
        return 1;
    }
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 ad{};
        a->GetDesc1(&ad);
        wprintf(L"Adapter %u: %ls (vendor 0x%04X, flags 0x%X)\n", i, ad.Description, ad.VendorId, ad.Flags);
        IDXGIOutput* o = nullptr;
        for (UINT j = 0; a->EnumOutputs(j, &o) != DXGI_ERROR_NOT_FOUND; ++j) {
            IDXGIOutput6* o6 = nullptr;
            if (SUCCEEDED(o->QueryInterface(__uuidof(IDXGIOutput6), (void**)&o6))) {
                DXGI_OUTPUT_DESC1 d{};
                o6->GetDesc1(&d);
                MONITORINFOEXW mi{};
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(d.Monitor, &mi);
                wprintf(L"  Output %u: %ls  monitor=%p  attached=%d  bits=%u  ColorSpace=%d%ls  "
                        L"lum min=%.3f max=%.1f fullframe=%.1f  rect=(%ld,%ld)-(%ld,%ld)  primary=%ls\n",
                        j, d.DeviceName, (void*)d.Monitor, (int)d.AttachedToDesktop, d.BitsPerColor,
                        (int)d.ColorSpace,
                        d.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? L" (PQ/2020 = HDR)" :
                        d.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? L" (sRGB = SDR)" : L"",
                        d.MinLuminance, d.MaxLuminance, d.MaxFullFrameLuminance,
                        d.DesktopCoordinates.left, d.DesktopCoordinates.top,
                        d.DesktopCoordinates.right, d.DesktopCoordinates.bottom,
                        (mi.dwFlags & MONITORINFOF_PRIMARY) ? L"yes" : L"no");
                o6->Release();
            } else {
                DXGI_OUTPUT_DESC d{};
                o->GetDesc(&d);
                wprintf(L"  Output %u: %ls (IDXGIOutput6 unavailable)\n", j, d.DeviceName);
            }
            o->Release();
        }
        a->Release();
    }
    f->Release();
    return 0;
}
