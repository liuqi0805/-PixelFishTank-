#include "App.h"
#include "Simulation.h"
#include "Persist.h"
#include "Util.h"

#include <windowsx.h>
#include <shellapi.h>
#include <cstring>

namespace ft {

namespace {

constexpr wchar_t kClassName[] = L"PixelFishTankWindow";

// 把 assets/ui/tray/icon.png 转成 HICON。
// 用 WIC 的缩放器统一到 32x32，所以“同名替换”成任意尺寸的 PNG 都能用。
HICON LoadIconFromPng(const std::wstring& path) {
    if (!FileExists(path)) return nullptr;

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wic)))) {
        return nullptr;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &decoder))) {
        return nullptr;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(&converter))) return nullptr;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }

    const UINT kSize = 32;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(wic->CreateBitmapScaler(&scaler))) return nullptr;
    if (FAILED(scaler->Initialize(converter.Get(), kSize, kSize,
                                  WICBitmapInterpolationModeNearestNeighbor))) {
        return nullptr;
    }

    std::vector<BYTE> pixels(static_cast<size_t>(kSize) * kSize * 4, 0);
    if (FAILED(scaler->CopyPixels(nullptr, kSize * 4,
                                  static_cast<UINT>(pixels.size()), pixels.data()))) {
        return nullptr;
    }

    BITMAPV5HEADER bi{};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = static_cast<LONG>(kSize);
    bi.bV5Height      = -static_cast<LONG>(kSize);
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    void* bits = nullptr;
    HDC screen = ::GetDC(nullptr);
    HBITMAP color = ::CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi),
                                       DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    if (!color || !bits) {
        if (color) ::DeleteObject(color);
        return nullptr;
    }
    std::memcpy(bits, pixels.data(), pixels.size());

    HBITMAP mask = ::CreateBitmap(static_cast<int>(kSize), static_cast<int>(kSize), 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmColor = color;
    ii.hbmMask  = mask;
    HICON icon = ::CreateIconIndirect(&ii);
    ::DeleteObject(color);
    if (mask) ::DeleteObject(mask);
    return icon;
}

// 同一线程内可重入的世界锁：SetWindowPos 等 API 会同步回调 WM_SIZE，
// 若在那里再次取独占锁会自锁，所以用线程局部深度做“已持有则不再取”。
thread_local int t_worldLockDepth = 0;
thread_local int t_readLockDepth  = 0;
thread_local bool t_upgradeWarn   = false;

class WorldLock {
public:
    explicit WorldLock(SRWLOCK& l) : lock_(l) {
        if (t_worldLockDepth++ == 0) {
            // 已持有共享锁却想升级成独占锁 = 必然自我死锁（SRW 不支持升级）。
            // 这里不硬崩：记下告警并沿用共享语义，让看门狗能报出问题。
            if (t_readLockDepth > 0) { t_upgradeWarn = true; return; }
            ::AcquireSRWLockExclusive(&lock_);
            acquired_ = true;
        }
    }
    ~WorldLock() {
        if (--t_worldLockDepth == 0 && acquired_) ::ReleaseSRWLockExclusive(&lock_);
    }
    WorldLock(const WorldLock&) = delete;
    WorldLock& operator=(const WorldLock&) = delete;

private:
    SRWLOCK& lock_;
    bool acquired_ = false;
};

// 旧实现用 t_worldLockDepth 反推“要不要释放共享锁”，一旦期间出现过独占锁就会漏释放，
// 共享锁永久泄漏 → 逻辑线程再也拿不到独占锁 → 鱼不动、界面僵死。
// 现在自己记住“到底取没取”，并且共享/独占深度分开计。
class WorldReadLock {
public:
    explicit WorldReadLock(SRWLOCK& l) : lock_(l) {
        if (t_worldLockDepth == 0 && t_readLockDepth == 0) {
            ::AcquireSRWLockShared(&lock_);
        }
        ++t_readLockDepth;
    }
    ~WorldReadLock() {
        --t_readLockDepth;
        if (t_readLockDepth == 0 && t_worldLockDepth == 0) ::ReleaseSRWLockShared(&lock_);
    }
    WorldReadLock(const WorldReadLock&) = delete;
    WorldReadLock& operator=(const WorldReadLock&) = delete;

private:
    SRWLOCK& lock_;
};

HCURSOR g_currentCursor = nullptr;

bool PointInRect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

} // namespace

App& App::Instance() {
    static App app;
    return app;
}

// -----------------------------------------------------------------------------
//  启动 / 收尾
// -----------------------------------------------------------------------------
int App::Run(HINSTANCE hInst) {
    hinst_ = hInst;
    if (!Init(hInst)) return 1;

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    // GetMessage 返回 0（WM_QUIT）之后才能真正回收线程
    Shutdown();
    return static_cast<int>(msg.wParam);
}

bool App::Init(HINSTANCE hInst) {
    const std::wstring assetsDir = PathJoin(ExeDirectory(), L"assets");
    if (!res_.Scan(assetsDir)) {
        std::wstring msg = L"资源目录不可用：\n" + assetsDir;
        for (const auto& e : res_.Errors()) msg += L"\n" + e;
        ::MessageBoxW(nullptr, msg.c_str(), L"像素鱼缸", MB_ICONWARNING | MB_OK);
    }

    // 设计坐标系下的默认世界
    world_.hwnd      = nullptr;
    world_.clientW   = kBaseWidth;
    world_.clientH   = kBaseHeight;
    world_.scale     = 1.0f;
    world_.water     = MakeWaterRect();

    InitializeWorld(world_, res_);

    // 读档（窗口尺寸/位置也从这里来）
    SavedWindow saved{};
    const bool loaded = LoadState(world_, res_, &saved);

    // 固定尺寸：窗口大小不提供缩放（用户反馈拖拽缩放会卡死，故整体移除）。
    // 只按当前显示器算一次「屏幕面积 / 24」的 4:3 尺寸，存档里的尺寸不再采用。
    int width = 0;
    int height = 0;
    {
        HMONITOR mon = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{ sizeof(MONITORINFO) };
        ::GetMonitorInfoW(mon, &mi);
        const double area = static_cast<double>(mi.rcMonitor.right - mi.rcMonitor.left) *
                            static_cast<double>(mi.rcMonitor.bottom - mi.rcMonitor.top);
        const double target = area / 24.0;
        width  = static_cast<int>(std::sqrt(target * static_cast<double>(kBaseWidth) / kBaseHeight) + 0.5);
        height = width * kBaseHeight / kBaseWidth;
    }

    int x = loaded && saved.valid ? saved.x : CW_USEDEFAULT;
    int y = loaded && saved.valid ? saved.y : CW_USEDEFAULT;

    // 需求要求无边框分层透明窗口：WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST。
    // 注意：WS_EX_TRANSPARENT 若常驻，窗口将收不到任何鼠标消息（连拖动/悬停都不行），
    // 所以这里按需求组合创建，真正生效的“透明区穿透”交给分层窗口的 alpha 命中测试
    // （alpha==0 的像素鼠标直接落到桌面图标），并在“锁定穿透”开关打开时才保留
    // WS_EX_TRANSPARENT。WS_EX_NOACTIVATE 保证置顶但不抢输入焦点。
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &App::WndProcStatic;
    wc.hInstance     = hInst;
    wc.hCursor       = nullptr;                  // 光标由 WM_SETCURSOR 自己管
    wc.hbrBackground = nullptr;                  // 分层窗口不刷背景
    wc.lpszClassName = kClassName;
    if (!::RegisterClassExW(&wc)) {
        if (::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                          WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    hwnd_ = ::CreateWindowExW(exStyle, kClassName, L"像素鱼缸",
                              WS_POPUP, x, y, width, height,
                              nullptr, nullptr, hInst, this);
    if (!hwnd_) {
        ::MessageBoxW(nullptr, L"创建窗口失败。", L"像素鱼缸", MB_ICONERROR | MB_OK);
        return false;
    }

    {
        WorldLock lk(lock_);
        world_.hwnd    = hwnd_;
        world_.clientW = width;
        world_.clientH = height;
        world_.scale   = ComputeScale(width);
        world_.water   = MakeWaterRect();
        ui_.UpdateLayout(world_, res_);
    }

    // 未锁定时去掉 WS_EX_TRANSPARENT，让 alpha>0 的区域能收鼠标消息
    ApplyClickThroughStyle();

    // 全局热键：Ctrl+Alt+F 隐藏/显示鱼缸（预留接口，可从设置里换成别的组合）
    hotkeyRegistered_ = ::RegisterHotKey(hwnd_, kHotkeyId,
                                         world_.settings.hotkeyMods, world_.settings.hotkeyVk) != FALSE;

    ApplySavedAutostart();

    // 托盘图标 + 看门狗（逻辑线程卡死时能把现场写进日志）
    CreateTray();
    ::SetTimer(hwnd_, kWatchdogId, 1500, nullptr);
    ::SetTimer(hwnd_, kCursorWatchId, 40, nullptr);
    watchdogLastTick_ = logicTick_.load();

    // 渲染线程自己创建 / 销毁 D2D 资源，避免跨线程使用渲染对象
    std::promise<bool> promise;
    auto future = promise.get_future();
    running_.store(true);
    renderThread_ = std::thread(&App::RenderLoop, this, std::move(promise));
    if (!future.get()) {
        running_.store(false);
        if (renderThread_.joinable()) renderThread_.join();
        std::wstring msg = L"Direct2D 初始化失败。\n";
        msg += renderer_.error();
        ::MessageBoxW(nullptr, msg.c_str(), L"像素鱼缸", MB_ICONERROR | MB_OK);
        return false;
    }

    // 等第一帧画好再显示，避免白闪
    for (int i = 0; i < 100 && !firstFrame_.load(); ++i) ::Sleep(5);

    ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    logicThread_ = std::thread(&App::LogicLoop, this);
    weather_.Start(world_.settings.weatherHost, world_.settings.weatherPath);

    {
        std::wstring tip = L"右键唤出菜单";
        if (res_.FishPacks().empty()) tip = L"assets\\fish 里没有资源包";
        WorldLock lk(lock_);
        ShowToast(world_, tip);
    }
    return true;
}

void App::Shutdown() {
    if (!running_.exchange(false)) {
        if (renderThread_.joinable()) renderThread_.join();
        if (logicThread_.joinable()) logicThread_.join();
        return;
    }
    ::KillTimer(hwnd_, kWatchdogId);
    ::KillTimer(hwnd_, kCursorWatchId);
    DestroyTray();
    weather_.Stop();
    if (logicThread_.joinable()) logicThread_.join();
    if (renderThread_.joinable()) renderThread_.join();   // 渲染线程内部销毁 renderer_
    if (hotkeyRegistered_) {
        ::UnregisterHotKey(hwnd_, kHotkeyId);
        hotkeyRegistered_ = false;
    }
}

void App::ApplySavedAutostart() {
    // 注册表里的实际状态和设置不一致时，以设置为准同步一次
    if (world_.settings.autostart != IsAutostartEnabled()) {
        ApplyAutostart(world_.settings.autostart);
    }
}

// -----------------------------------------------------------------------------
//  渲染线程（60fps）
// -----------------------------------------------------------------------------
void App::RenderLoop(std::promise<bool> created) {
    // WIC 走 COM，渲染线程必须自己初始化（STA/MTA 均可，这里用 MTA 更省事）
    const HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // 注意：ready() 表示“后台缓冲已就绪”，要等 EnsureSize 之后才为 true，
    // 所以这里必须用 Create() 的返回值判断是否初始化成功。
    if (!renderer_.Create(hwnd_)) {
        created.set_value(false);
        if (SUCCEEDED(comHr)) ::CoUninitialize();
        return;
    }
    created.set_value(true);

    using clock = std::chrono::steady_clock;
    const auto frameBudget = std::chrono::microseconds(16667);   // 60fps
    auto next = clock::now();
    auto last = clock::now();

    while (running_.load()) {
        const auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.25f) dt = 0.25f;   // 挂起/断点造成的巨大跳变拉回来

        {
            WorldLock lk(lock_);
            world_.animClock += dt;
        }
        {
            WorldReadLock lk(lock_);
            const int w = world_.clientW;
            const int h = world_.clientH;
            if (w > 0 && h > 0 && renderer_.EnsureSize(w, h) && renderer_.Begin()) {
                ui_.Render(world_, renderer_, res_);
                renderer_.End();
                if (!firstFrame_.load()) firstFrame_.store(true);
            }
        }

        renderTick_.fetch_add(1);

        const auto after = clock::now();
        if (!visible_.load()) {
            // 隐藏到托盘：逻辑照跑（鱼照样饿、照样耗氧），画面降到 ~5fps 省 CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(180));
            next = clock::now();
            continue;
        }

        next += frameBudget;
        if (after < next) {
            std::this_thread::sleep_until(next);
        } else {
            next = after;   // 掉帧就重新对齐，不累积
        }
    }

    renderer_.Destroy();   // 必须在本线程销毁
    if (SUCCEEDED(comHr)) ::CoUninitialize();
}

// -----------------------------------------------------------------------------
//  逻辑线程（10Hz）
// -----------------------------------------------------------------------------
void App::LogicLoop() {
    using clock = std::chrono::steady_clock;
    const float step = 0.1f;
    auto last = clock::now();
    float acc = 0.0f;

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto now = clock::now();
        acc += std::chrono::duration<float>(now - last).count();
        last = now;
        if (acc > 1.0f) acc = step;   // 别补太多帧

        while (acc >= step) {
            acc -= step;
            WorldLock lk(lock_);
            DrainActions();                      // 消息线程攒下的世界动作在这里统一执行
            StepSimulation(world_, res_, step);
            ui_.UpdateLayout(world_, res_);
            if (world_.ui.mouseInside) {
                const HitResult hit = ui_.HitTest(world_, world_.ui.mouse, res_);
                ui_.ApplyHit(world_, hit);       // 只同步 hoverAction / 菜单悬停
            }
            logicTick_.fetch_add(1);             // 看门狗心跳
        }

        WeatherResult wr;
        if (weather_.Take(wr)) {
            WorldLock lk(lock_);
            world_.ambientTemp = wr.tempC;
            world_.weatherOnline = wr.online;
            world_.weatherNote = wr.note;
            world_.weatherAge = 0.0f;
        }
    }
}

// -----------------------------------------------------------------------------
//  窗口过程
// -----------------------------------------------------------------------------
LRESULT CALLBACK App::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<App*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(hwnd, msg, wp, lp);
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        return OnSize(wp, lp);

    // 调试用：把当前命中相关的矩形导出到 %TEMP% 下的 pft_debug.txt
    // （自动化测试判断“按钮到底画在哪里”时用，正常运行不会触发；
    //   字段之间用 | 分隔，避免依赖换行转义）
    case WM_APP + 8:   // 调试用：把托盘右键菜单弹出来（自动化测试用，正常交互不会触发）
        ShowTrayMenu();
        return 0;

    case WM_APP + 7: {
        WorldLock lk(lock_);
        auto rectText = [](const Rect& r) {
            return L"(" + std::to_wstring(static_cast<int>(r.l)) + L"," +
                   std::to_wstring(static_cast<int>(r.t)) + L"," +
                   std::to_wstring(static_cast<int>(r.r)) + L"," +
                   std::to_wstring(static_cast<int>(r.b)) + L")";
        };
        std::wstring s;
        s += L"client=" + std::to_wstring(world_.clientW) + L"x" +
             std::to_wstring(world_.clientH) + L"|";
        s += L"scale=" + std::to_wstring(world_.scale) + L"|";
        s += L"hoverFish=" + std::to_wstring(world_.ui.hoverFish) + L"|";
        s += L"mouseInside=" + std::to_wstring(world_.ui.mouseInside ? 1 : 0) + L"|";
        s += L"mouse=" + rectText(Rect::FromXYWH(world_.ui.mouse.x, world_.ui.mouse.y, 0, 0)) + L"|";
        s += L"buttonsVisible=" + std::to_wstring(world_.ui.fishButtonsVisible ? 1 : 0) + L"|";
        s += L"feedBtn=" + rectText(world_.ui.feedBtn) + L"|";
        s += L"teaseBtn=" + rectText(world_.ui.teaseBtn) + L"|";
        s += L"panelVisible=" + std::to_wstring(world_.ui.panelVisible ? 1 : 0) + L"|";
        s += L"panelRect=" + rectText(world_.ui.panelRect) + L"|";
        s += L"aerator=" + std::to_wstring(world_.equip.aerator ? 1 : 0) + L"|";
        s += L"heater=" + std::to_wstring(world_.equip.heater ? 1 : 0) + L"|";
        s += L"toast=" + world_.ui.toast + L"|";
        s += L"menuOpen=" + std::to_wstring(world_.ui.menuOpen ? 1 : 0) + L"|";
        s += L"buttonOwner=" + std::to_wstring(world_.ui.buttonOwner) + L"|";
        s += L"holdLeft=" + std::to_wstring(world_.ui.buttonHold - world_.animClock) + L"|";
        s += L"animClock=" + std::to_wstring(world_.animClock) + L"|";
        s += L"menuBtn=" + rectText(world_.ui.menuButton) + L"|";
        s += L"locked=" + std::to_wstring(world_.settings.clickThroughLocked ? 1 : 0) + L"|";
        s += L"hot=" + std::to_wstring(hotHamburger_) + L"|";
        s += L"menuRect=" + rectText(world_.ui.menuRect) + L"|";
        s += L"menuCount=" + std::to_wstring(world_.ui.menuCount) + L"|";
        {
            std::wstring mt;
            for (int i = 0; i < world_.ui.menuCount; ++i) {
                if (i) mt += L",";
                mt += world_.ui.menuTexts[i];
            }
            s += L"menu=" + mt + L"|";
        }
        s += L"closeToTray=" + std::to_wstring(world_.settings.closeToTray ? 1 : 0) + L"|";
        s += L"visible=" + std::to_wstring(visible_.load() ? 1 : 0) + L"|";
        s += L"tray=" + std::to_wstring(trayAdded_ ? 1 : 0) + L"|";
        s += L"logicTick=" + std::to_wstring(logicTick_.load()) + L"|";
        s += L"renderTick=" + std::to_wstring(renderTick_.load()) + L"|";
        if (!world_.fish.empty() && world_.fish[0].used) {
            const Fish& f0 = world_.fish[0];
            s += L"satiety=" + std::to_wstring(f0.satiety) + L"|";
            s += L"mood=" + std::to_wstring(f0.mood) + L"|";
            s += L"obesity=" + std::to_wstring(f0.obesity) + L"|";
        }
        for (int i = 0; i < static_cast<int>(world_.fish.size()); ++i) {
            const Fish& f = world_.fish[i];
            if (!f.used) continue;
            const Rect box = Rect::FromXYWH(f.pos.x, f.pos.y, 0, 0);
            s += L"fish" + std::to_wstring(i) + L".pos=" + rectText(box) + L"|";
        }
        wchar_t tmp[MAX_PATH] = { 0 };
        ::GetTempPathW(static_cast<DWORD>(std::size(tmp)), tmp);
        WriteWholeFileBytes(PathJoin(tmp, L"pft_debug.txt"), WideToUtf8(s));
        return 0;
    }

    case WM_MOUSEMOVE:
        OnMouseMove(lp);
        return 0;

    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;

    case WM_LBUTTONDOWN:
        ::SetCapture(hwnd_);
        OnLeftButtonDown(lp);
        return 0;

    case WM_LBUTTONUP:
        OnLeftButtonUp(lp);
        if (::GetCapture() == hwnd_) ::ReleaseCapture();
        return 0;

    case WM_RBUTTONDOWN:
        OnRightButtonDown(lp);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            ::SetCursor(g_currentCursor ? g_currentCursor : ::LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;

    case kTrayMsg:
        switch (static_cast<UINT>(lp)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            if (::IsWindowVisible(hwnd_)) HideTankWindow();
            else                          ShowTankWindow();
            return 0;
        case WM_RBUTTONUP:
            ShowTrayMenu();
            return 0;
        default:
            break;
        }
        return 0;

    case WM_HOTKEY:
        if (static_cast<int>(wp) == kHotkeyId) {
            OnHotkey();
            return 0;
        }
        break;

    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        if (suggested) {
            ::SetWindowPos(hwnd_, HWND_TOPMOST,
                           suggested->left, suggested->top,
                           suggested->right - suggested->left,
                           suggested->bottom - suggested->top,
                           SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_DISPLAYCHANGE:
        return 0;

    case WM_TIMER:
        if (wp == kCursorWatchId) {
            UpdateClickThroughByCursor();
            return 0;
        }
        if (wp == kWatchdogId) {
            CheckWatchdog();
            return 0;
        }
        break;

    case WM_MOUSEACTIVATE:
        // 置顶但不抢输入焦点
        return MA_NOACTIVATE;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            WorldLock lk(lock_);
            ui_.CloseMenu(world_);
            ui_.UpdateLayout(world_, res_);
            return 0;
        }
        break;

    case WM_CLOSE:
        {
            bool park = false;
            {
                WorldLock lk(lock_);
                SaveState(world_);
                park = world_.settings.closeToTray && !quitting_;
            }
            if (park) {
                HideTankWindow();   // 收进托盘：进程活着，鱼照样在跑
                return 0;
            }
        }
        DestroyTray();
        ::DestroyWindow(hwnd_);
        return 0;

    case WM_QUERYENDSESSION:
        {
            WorldLock lk(lock_);
            SaveState(world_);
        }
        return TRUE;

    case WM_ENDSESSION:
        if (wp) {
            WorldLock lk(lock_);
            SaveState(world_);
        }
        return 0;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    default:
        // 资源管理器重启会清空托盘图标，收到广播就把图标重新挂上
        if (trayTaskbarCreated_ != 0 && msg == trayTaskbarCreated_) {
            trayAdded_ = false;
            CreateTray();
            return 0;
        }
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::OnSize(WPARAM wParam, LPARAM lp) {
    const int w = LOWORD(lp);
    const int h = HIWORD(lp);
    (void)wParam;
    if (w <= 0 || h <= 0) return 0;

    WorldLock lk(lock_);
    world_.clientW = w;
    world_.clientH = h;
    world_.scale   = ComputeScale(w);
    world_.water   = MakeWaterRect();
    ui_.UpdateLayout(world_, res_);
    return 0;
}

// -----------------------------------------------------------------------------
//  鼠标
// -----------------------------------------------------------------------------
void App::RefreshHitTest(float clientX, float clientY) {
    WorldLock lk(lock_);
    const Vec2 dp = ClientToDesign(clientX, clientY, world_.scale);
    world_.ui.mouse = dp;
    world_.ui.mouseInside = true;
    ui_.UpdateLayout(world_, res_);
    const HitResult hit = ui_.HitTest(world_, dp, res_);
    ui_.ApplyHit(world_, hit);

    if (!trackingLeave_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        if (::TrackMouseEvent(&tme)) trackingLeave_ = true;
    }

    if (hit.AnyUi() || hit.inWater) {
        g_currentCursor = ::LoadCursorW(nullptr, IDC_HAND);
    } else {
        g_currentCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    }
}

void App::OnMouseMove(LPARAM lp) {
    if (dragging_) {
        OnDragMove();
        return;
    }
    RefreshHitTest(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
}

void App::OnMouseLeave() {
    trackingLeave_ = false;
    WorldLock lk(lock_);
    world_.ui.mouseInside = false;
    world_.ui.hoverFish = -1;
    world_.ui.hoverAction = -1;
    world_.ui.hoverMenuIndex = -1;
    ui_.UpdateLayout(world_, res_);
}

void App::OnLeftButtonDown(LPARAM lp) {
    const float cx = static_cast<float>(GET_X_LPARAM(lp));
    const float cy = static_cast<float>(GET_Y_LPARAM(lp));

    WorldLock lk(lock_);
    const Vec2 dp = ClientToDesign(cx, cy, world_.scale);
    world_.ui.mouse = dp;
    world_.ui.mouseInside = true;
    ui_.UpdateLayout(world_, res_);
    const HitResult hit = ui_.HitTest(world_, dp, res_);
    ui_.ApplyHit(world_, hit);

    // 1) 菜单
    if (hit.menuOpen) {
        if (hit.menuIndex >= 0 && hit.menuIndex < world_.ui.menuCount) {
            const UiAction action = world_.ui.menuActions[hit.menuIndex];
            ui_.CloseMenu(world_);
            HandleAction(action, -1);
        } else {
            ui_.CloseMenu(world_);
            ui_.UpdateLayout(world_, res_);
        }
        return;
    }

    // 2) 常驻菜单入口（右上角 ☰）：点一下展开 / 收起
    if (hit.menuButton) {
        if (world_.ui.menuOpen) {
            ui_.CloseMenu(world_);
        } else {
            const Rect& b = world_.ui.menuButton;
            ui_.BuildMenu(world_, Vec2{ b.l, b.b + 2.0f });
        }
        ui_.UpdateLayout(world_, res_);
        return;
    }

    // 4) 鱼身上的按钮（用命中结果里的鱼索引，而不是 hoverFish——
    //    鼠标停在按钮上时已经离开鱼身，hoverFish 此时是 -1）
    if (hit.fishButton != UiAction::None) {
        HandleAction(hit.fishButton, hit.fish.index);
        ui_.UpdateLayout(world_, res_);
        return;
    }

    // 5) 水体操作面板
    if (hit.panelButton != UiAction::None) {
        HandleAction(hit.panelButton, -1);
        ui_.UpdateLayout(world_, res_);
        return;
    }

    // 6) 鱼本体：直接点按视为挑逗
    if (hit.fish.hit) {
        HandleAction(UiAction::Tease, hit.fish.index);
        ui_.UpdateLayout(world_, res_);
        return;
    }

    // 7) 水体空白区域：拖动整个窗口
    if (hit.inWater) {
        dragging_ = true;
        world_.ui.dragging = true;
        ::GetCursorPos(&dragStartCursor_);
        ::GetWindowRect(hwnd_, &dragStartRect_);
    }
}

void App::OnLeftButtonUp(LPARAM lp) {
    (void)lp;
    dragging_ = false;
    {
        WorldLock lk(lock_);
        world_.ui.dragging = false;
        ui_.UpdateLayout(world_, res_);
    }
    g_currentCursor = ::LoadCursorW(nullptr, IDC_HAND);
    ::SetCursor(g_currentCursor);
}

void App::OnRightButtonDown(LPARAM lp) {
    // 鱼缸画面内右键唤起菜单（锁了穿透时窗口收不到消息，自然不会触发）
    WorldLock lk(lock_);
    if (world_.settings.clickThroughLocked) return;
    const Vec2 p = ClientToDesign(static_cast<float>(GET_X_LPARAM(lp)),
                                  static_cast<float>(GET_Y_LPARAM(lp)), world_.scale);
    ui_.BuildMenu(world_, p);
    ui_.UpdateLayout(world_, res_);
}

void App::OnDragMove() {
    POINT cur{};
    ::GetCursorPos(&cur);
    const int dx = cur.x - dragStartCursor_.x;
    const int dy = cur.y - dragStartCursor_.y;
    ::SetWindowPos(hwnd_, HWND_TOPMOST,
                   dragStartRect_.left + dx, dragStartRect_.top + dy,
                   0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}


void App::OnHotkey() {
    if (!hwnd_) return;
    if (::IsWindowVisible(hwnd_)) {
        HideTankWindow();
        WorldLock lk(lock_);
        ShowToast(world_, L"已隐藏，Ctrl+Alt+F 恢复");
    } else {
        ShowTankWindow();
    }
}

// -----------------------------------------------------------------------------
//  托盘
// -----------------------------------------------------------------------------
bool App::CreateTray() {
    if (trayAdded_) return true;
    if (!hwnd_) return false;

    if (!trayTaskbarCreated_) {
        trayTaskbarCreated_ = ::RegisterWindowMessageW(L"TaskbarCreated");
    }

    if (!trayIcon_) {
        const std::wstring iconPath =
            PathJoin(PathJoin(PathJoin(res_.AssetsDir(), L"ui"), L"tray"), L"icon.png");
        trayIcon_ = LoadIconFromPng(iconPath);
        if (!trayIcon_) trayIcon_ = ::LoadIconW(nullptr, IDI_APPLICATION);   // 缺图标也不崩
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd_;
    nid.uID              = kTrayId;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon            = trayIcon_;
    wcsncpy_s(nid.szTip, L"像素鱼缸 · 单击显示/隐藏，右键菜单", _TRUNCATE);
    trayAdded_ = ::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (trayAdded_) UpdateTrayTip();
    return trayAdded_;
}

void App::DestroyTray() {
    if (trayAdded_ && hwnd_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd_;
        nid.uID    = kTrayId;
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
        trayAdded_ = false;
    }
    if (trayIcon_) {
        if (trayIcon_ != ::LoadIconW(nullptr, IDI_APPLICATION)) ::DestroyIcon(trayIcon_);
        trayIcon_ = nullptr;
    }
    if (trayOwner_) {
        ::DestroyWindow(trayOwner_);
        trayOwner_ = nullptr;
    }
}

void App::UpdateTrayTip() {
    if (!trayAdded_ || !hwnd_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = kTrayId;
    nid.uFlags = NIF_TIP;
    int alive = 0;
    {
        WorldLock lk(lock_);
        alive = CountAliveFish(world_);
    }
    swprintf_s(nid.szTip, L"像素鱼缸 · %d 条鱼 · %s", alive,
               (::IsWindowVisible(hwnd_) ? L"单击隐藏" : L"单击显示"));
}

void App::ShowTrayMenu() {
    if (!hwnd_) return;

    // 主窗口带 WS_EX_NOACTIVATE，直接拿它当 TrackPopupMenu 的 owner 菜单会一闪就没；
    // 这里用 0x0 的普通隐藏窗口当 owner，弹系统菜单最稳。
    if (!trayOwner_) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ::DefWindowProcW;
        wc.hInstance     = hinst_;
        wc.lpszClassName = L"PixelFishTankTrayOwner";
        ::RegisterClassExW(&wc);
        trayOwner_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP,
                                       0, 0, 0, 0, nullptr, nullptr, hinst_, nullptr);
    }

    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    const bool visible = ::IsWindowVisible(hwnd_) != FALSE;
    bool autostart = false;
    bool closeToTray = true;
    {
        WorldLock lk(lock_);
        autostart   = world_.settings.autostart;
        closeToTray = world_.settings.closeToTray;
    }

    ::AppendMenuW(menu, MF_STRING, 1, visible ? L"隐藏鱼缸" : L"显示鱼缸");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, 2, autostart ? L"开机自启：开" : L"开机自启：关");
    ::AppendMenuW(menu, MF_STRING, 3, closeToTray ? L"关闭到托盘：开" : L"关闭到托盘：关");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, 4, L"退出程序");

    POINT pt{};
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(trayOwner_);
    const int cmd = ::TrackPopupMenu(menu,
                                     TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, trayOwner_, nullptr);
    ::PostMessageW(trayOwner_, WM_NULL, 0, 0);
    ::DestroyMenu(menu);

    switch (cmd) {
    case 1:
        if (::IsWindowVisible(hwnd_)) HideTankWindow();
        else                          ShowTankWindow();
        break;
    case 2: {
        WorldLock lk(lock_);
        HandleAction(UiAction::ToggleAutostart, -1);
    } break;
    case 3: {
        WorldLock lk(lock_);
        HandleAction(UiAction::ToggleCloseToTray, -1);
    } break;
    case 4:
        quitting_ = true;
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
    UpdateTrayTip();
}

void App::ShowTankWindow() {
    if (!hwnd_) return;
    visible_.store(true);
    ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateTrayTip();
}

void App::HideTankWindow() {
    if (!hwnd_) return;
    visible_.store(false);
    ::ShowWindow(hwnd_, SW_HIDE);
    {
        WorldLock lk(lock_);
        world_.ui.mouseInside = false;
        world_.ui.dragging = false;
    }
    dragging_ = false;
    UpdateTrayTip();
}

// -----------------------------------------------------------------------------
//  看门狗：逻辑线程若是几秒没推进，就把现场写到 %TEMP% 下的日志里，方便查卡死
// -----------------------------------------------------------------------------
void App::CheckWatchdog() {
    const unsigned long long tick = logicTick_.load();
    if (tick == watchdogLastTick_) {
        if (++watchdogStuckCount_ >= 3) {
            watchdogStuckCount_ = 0;
            WriteFreezeLog(L"逻辑线程连续 4.5 秒没有推进");
        }
    } else {
        watchdogStuckCount_ = 0;
    }
    watchdogLastTick_ = tick;
}

void App::WriteFreezeLog(const wchar_t* reason) {
    wchar_t temp[MAX_PATH] = {};
    if (::GetTempPathW(MAX_PATH, temp) == 0) return;
    const std::wstring path = std::wstring(temp) + L"pft_freezelog.txt";

    // 探一下世界锁到底是不是被谁攥着（TryAcquire 成功说明锁是空的）
    const bool lockFree = ::TryAcquireSRWLockExclusive(&lock_) != FALSE;
    if (lockFree) ::ReleaseSRWLockExclusive(&lock_);

    // 拼一行文本：这里刻意不用转义字符写换行（本环境 heredoc 会吃掉反斜杠）
    std::wstring text = L"[";
    text += std::to_wstring(static_cast<unsigned long long>(WallClockSeconds()));
    text += L"] ";
    text += reason;
    text += L" | logicTick=" + std::to_wstring(logicTick_.load());
    text += L" renderTick=" + std::to_wstring(renderTick_.load());
    text += lockFree ? L" lockFree=1" : L" lockFree=0";
    text += t_upgradeWarn ? L" 共享锁内升级独占锁=1" : L" 共享锁内升级独占锁=0";
    text.push_back(static_cast<wchar_t>(10));

    const std::string utf8 = WideToUtf8(text);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"ab") == 0 && fp) {
        std::fwrite(utf8.data(), 1, utf8.size(), fp);
        std::fclose(fp);
    }
}

// -----------------------------------------------------------------------------
//  动作分发
// -----------------------------------------------------------------------------
// 世界动作统一交给逻辑线程执行：消息线程只入队，绝不长时间霸占世界锁。
// （以前是 UI 线程直接 ApplyAction，连点几十下就把渲染/逻辑线程饿死了）
void App::EnqueueAction(UiAction action, int fishIndex) {
    {
        std::lock_guard<std::mutex> g(queueMutex_);
        if (pending_.size() >= 64) pending_.erase(pending_.begin());   // 病态连点兜底
        pending_.push_back(PendingAction{ action, fishIndex });
    }
}

void App::DrainActions() {
    std::vector<PendingAction> batch;
    {
        std::lock_guard<std::mutex> g(queueMutex_);
        batch.swap(pending_);
    }
    for (const PendingAction& pa : batch) {
        ApplyAction(world_, res_, pa.action, pa.fishIndex);
    }
}

void App::HandleAction(UiAction action, int fishIndex) {
    switch (action) {
    case UiAction::Feed:
    case UiAction::Tease:
    case UiAction::ToggleAerator:
    case UiAction::ToggleHeater:
    case UiAction::AddPlant:
    case UiAction::AddFish:
    case UiAction::ClearTank:
    case UiAction::ResetEnv:
        EnqueueAction(action, fishIndex);   // 逻辑线程 100ms 内执行
        break;


    case UiAction::ToggleClickThrough:
        world_.settings.clickThroughLocked = !world_.settings.clickThroughLocked;
        ApplyClickThroughStyle();
        ShowToast(world_, world_.settings.clickThroughLocked
                                   ? L"已锁定穿透（右键菜单可再解开）"
                                   : L"已解除穿透锁定");
        break;

    case UiAction::ToggleAutostart:
        world_.settings.autostart = !world_.settings.autostart;
        if (!ApplyAutostart(world_.settings.autostart)) {
            world_.settings.autostart = !world_.settings.autostart;
            ShowToast(world_, L"开机自启写入注册表失败");
        } else {
            ShowToast(world_, world_.settings.autostart ? L"已开启开机自启"
                                                             : L"已关闭开机自启");
        }
        break;

    case UiAction::ToggleCloseToTray:
        world_.settings.closeToTray = !world_.settings.closeToTray;
        ShowToast(world_, world_.settings.closeToTray ? L"关闭时将隐藏到托盘"
                                                     : L"关闭时将直接退出程序");
        break;

    case UiAction::Quit:
        quitting_ = true;                          // 「退出程序」必须真的退出（不改持久化设置）
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;

    case UiAction::None:
    default:
        break;
    }
}

void App::ApplyClickThroughStyle() {
    SetClickThrough(world_.settings.clickThroughLocked);
}

void App::SetClickThrough(bool on) {
    if (!hwnd_) return;
    LONG_PTR ex = ::GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    const bool now = (ex & WS_EX_TRANSPARENT) != 0;
    if (now == on) return;                       // 状态没变就别折腾窗口
    if (on) ex |= WS_EX_TRANSPARENT;
    else    ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    ::SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
    ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// 锁定鼠标穿透时整个窗口是 WS_EX_TRANSPARENT（收不到鼠标消息），一刀切常驻的话
// 右上角 ☰ 也会一起穿透、菜单再也点不开。这里用 40ms 定时器轮询光标：
// 只有光标压在 ☰ 或已打开的菜单上时才临时去掉 WS_EX_TRANSPARENT（这一小块不穿透），
// 离开就恢复全穿透，水面上右键仍然落到桌面。
void App::UpdateClickThroughByCursor() {
    if (!hwnd_ || !world_.settings.clickThroughLocked) return;
    POINT pt{};
    if (!::GetCursorPos(&pt)) return;
    ::ScreenToClient(hwnd_, &pt);
    const float scale = world_.scale > 0.0f ? world_.scale : 1.0f;
    const Vec2 dp = ClientToDesign(static_cast<float>(pt.x), static_cast<float>(pt.y), scale);

    bool hot = false;
    {
        WorldReadLock lk(lock_);
        if (world_.ui.menuButton.Contains(dp)) hot = true;
        if (!hot && world_.ui.menuOpen && world_.ui.menuRect.Contains(dp)) hot = true;
    }
    hotHamburger_ = hot ? 1 : 0;
    SetClickThrough(!hot);
}

} // namespace ft
