// =============================================================================
//  App.h  --  窗口 / 消息循环 / 渲染线程 / 逻辑线程 / 交互
//
//  线程模型：
//    * 消息线程：窗口过程、鼠标交互、热键；只做“瞬时状态”更新
//    * 渲染线程：60fps，独占 Renderer 与 D2D 资源
//    * 逻辑线程：10Hz，跑生态模拟（与渲染完全解耦）
//  共享数据 World 用 SRWLOCK 保护：渲染读（共享），其余写（独占）。
// =============================================================================
#pragma once

#include "Common.h"
#include "Model.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include "Ui.h"
#include "Weather.h"

#include <future>
#include <thread>
#include <vector>

namespace ft {

struct PendingAction {
    UiAction action = UiAction::None;
    int      fishIndex = -1;
};

class App {
public:
    static App& Instance();

    int Run(HINSTANCE hInst);

private:
    App() = default;
    ~App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    bool Init(HINSTANCE hInst);
    void Shutdown();

    void RenderLoop(std::promise<bool> created);
    void LogicLoop();

    // ---- 消息处理 ----
    void OnMouseMove(LPARAM lp);
    void OnMouseLeave();
    void OnLeftButtonDown(LPARAM lp);
    void OnLeftButtonUp(LPARAM lp);
    void OnRightButtonDown(LPARAM lp);
    void OnDragMove();
    void OnHotkey();
    LRESULT OnSize(WPARAM wParam, LPARAM lp);

    // ---- 交互 ----
    void RefreshHitTest(float clientX, float clientY);
    void HandleAction(UiAction action, int fishIndex);   // 世界动作只入队，由逻辑线程执行
    void ApplyClickThroughStyle();
    void SetClickThrough(bool on);
    void UpdateClickThroughByCursor();
    void ApplySavedAutostart();
    void EnqueueAction(UiAction action, int fishIndex);
    void DrainActions();                                 // 逻辑线程调用（持独占锁）

    // ---- 托盘 ----
    bool CreateTray();
    void DestroyTray();
    void UpdateTrayTip();
    void ShowTrayMenu();
    void ShowTankWindow();
    void HideTankWindow();
    void ReleaseMouseCapture();

    // ---- 看门狗（诊断逻辑线程是否卡住） ----
    void CheckWatchdog();
    void WriteFreezeLog(const wchar_t* reason);

    // ---- 尺寸 ----

    HWND      hwnd_     = nullptr;
    HINSTANCE hinst_    = nullptr;

    World     world_{};
    ResourceManager res_;
    Renderer  renderer_;
    Ui        ui_;
    WeatherClient weather_;

    SRWLOCK lock_ = SRWLOCK_INIT;

    std::atomic<bool> running_{ false };
    std::atomic<bool> firstFrame_{ false };
    std::atomic<bool> visible_{ true };     // 隐藏到托盘时渲染降频
    std::atomic<unsigned long long> logicTick_{ 0 };
    std::atomic<unsigned long long> renderTick_{ 0 };
    std::thread renderThread_;
    std::thread logicThread_;

    // 世界动作队列：消息线程只入队，逻辑线程统一消费，
    // 这样连点不会让 UI 线程长时间霸占世界锁。
    std::mutex               queueMutex_;
    std::vector<PendingAction> pending_;

    bool dragging_ = false;
    POINT dragStartCursor_{ 0, 0 };
    RECT  dragStartRect_{ 0, 0, 0, 0 };

    bool trackingLeave_ = false;
    bool hotkeyRegistered_ = false;
    bool quitting_ = false;
    int  hotHamburger_ = -1; // 最近一次光标轮询结果（-1=没检测）
    static constexpr int kHotkeyId  = 0x4B31;
    static constexpr UINT kTrayMsg  = WM_APP + 21;
    static constexpr UINT kTrayId   = 1;
    static constexpr UINT_PTR kWatchdogId = 2;
    static constexpr UINT_PTR kCursorWatchId = 3;   // 锁定穿透时轮询光标，决定 ☰ 是否可点

    // 托盘
    bool  trayAdded_ = false;
    HICON trayIcon_  = nullptr;
    HWND  trayOwner_ = nullptr;   // 用来挂 TrackPopupMenu 的普通（非 NOACTIVATE）窗口
    UINT  trayTaskbarCreated_ = 0;

    // 看门狗
    unsigned long long watchdogLastTick_ = 0;
    int watchdogStuckCount_ = 0;
};

} // namespace ft
