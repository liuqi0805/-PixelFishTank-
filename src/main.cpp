// =============================================================================
//  main.cpp  --  入口：高 DPI 感知、单实例、COM 初始化，然后进入 App
//
//  普通用户权限运行，不需要管理员：只用 HKCU 注册表 + 用户目录写文件。
// =============================================================================
#include "Common.h"
#include "App.h"

#include <objbase.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {

constexpr const wchar_t* kMutexName = L"PixelFishTank.SingleInstance.4F1B";

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // 1) Per-Monitor V2 高 DPI 感知：必须在创建任何窗口之前设置
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 2) 单实例：重复启动直接退出，避免两个鱼缸叠加
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(mutex);
        return 0;
    }

    // 3) COM（WIC / D2D 需要）
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    const int code = ft::App::Instance().Run(hInst);

    if (SUCCEEDED(hr)) ::CoUninitialize();
    if (mutex) {
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
    }
    return code;
}
