// =============================================================================
//  Model.h  --  全局共享状态（逻辑线程写 / 渲染线程与消息线程读，SRWLOCK 保护）
// =============================================================================
#pragma once

#include "Common.h"
#include "Sprite.h"

namespace ft {

// ---------------------------------------------------------------------------
// 可点击的动作（UI 与菜单共用）
// ---------------------------------------------------------------------------
enum class UiAction : int {
    None = 0,
    Feed,             // 喂食
    Tease,            // 挑逗
    ToggleAerator,    // 放置 / 移除 增氧器
    ToggleHeater,     // 放置 / 移除 加温棒
    AddPlant,         // 添加水草
    AddFish,          // 添加鱼
    ClearTank,        // 清空鱼缸
    ResetEnv,         // 重置环境
    ToggleClickThrough, // 锁定穿透（WS_EX_TRANSPARENT 开关）
    ToggleAutostart,  // 开机自启（注册表）
    ToggleCloseToTray,// 点关闭时隐藏到托盘 / 直接退出
    Quit              // 退出程序
};

// ---------------------------------------------------------------------------
// 单条鱼
// ---------------------------------------------------------------------------
struct Fish {
    bool         used = false;          // 槽位是否被占用
    std::wstring packName;              // 资源包名（assets\fish\<packName>）
    std::wstring displayName;

    Vec2  pos{};                        // 设计坐标（鱼帧中心）
    Vec2  target{};
    float facing = 1.0f;                // 1 = 朝右，-1 = 朝左
    float bobPhase = 0.0f;              // 上下轻摆的相位
    float speed = 20.0f;                // 设计像素/秒

    // 生态状态
    float satiety  = 70.0f;             // 饱食度 0..100
    float obesity  = 0.0f;              // 肥胖等级 0..1
    float mood     = 80.0f;             // 心情 0..100
    bool  alive    = true;

    // 计时器
    float starveTimer   = 0.0f;         // 饱食度为 0 的持续时间
    float lowOxyTimer   = 0.0f;         // 溶氧低于阈值的累计时间
    float deadTimer     = 0.0f;         // 死亡后停留时间
    float idleWait      = 0.0f;         // 到达目标后的发呆时间
    float actionTimer   = 0.0f;         // feed/tease 动画剩余时间
    float age           = 0.0f;

    // 动画
    FishAnim anim       = FishAnim::Idle;
    FishAnim actionAnim = FishAnim::Idle;   // actionTimer 期间要播放的动画
    float    animTime   = 0.0f;
};

// ---------------------------------------------------------------------------
// 场景物件
// ---------------------------------------------------------------------------
struct Plant {
    std::wstring packName;
    Vec2  pos{};
    float scale = 1.0f;
    float swayPhase = 0.0f;
};

struct Equipment {
    bool aerator = false;
    Vec2 aeratorPos{};
    bool heater = false;
    Vec2 heaterPos{};
    float heaterTarget = 26.0f;   // 加温棒目标水温
};

struct Env {
    float waterTemp = 24.0f;      // 水温 ℃
    float oxygen    = 85.0f;      // 溶氧量 0..100（百分比）
};

// ---------------------------------------------------------------------------
// UI 瞬时状态（布局由 Ui::UpdateLayout 在逻辑 tick 里算好，消息线程只读）
// ---------------------------------------------------------------------------
struct UiState {
    Vec2 mouse{};                  // 设计坐标
    bool mouseInside  = false;
    int  hoverFish    = -1;        // 鼠标悬停的鱼槽位
    int  hoverAction  = -1;        // 当前悬停按钮（UiAction 的 int 值，-1 = 无）

    // 鱼身上的两个按钮
    bool fishButtonsVisible = false;
    Rect feedBtn{};
    Rect teaseBtn{};
    // 按钮归属哪条鱼 + 保持到哪个时刻（animClock）。
    // 鼠标离开鱼身后按钮不会立刻消失，否则“把鼠标移过去点按钮”根本做不到。
    int   buttonOwner = -1;
    float buttonHold  = 0.0f;

    // 常驻菜单入口（水体右上角的像素按钮，不依赖悬停）
    Rect menuButton{};

    // 水体空白区域的操作面板
    bool panelVisible = false;
    Rect panelRect{};
    Rect aeratorBtn{};
    Rect heaterBtn{};

    // 右键菜单
    bool menuOpen = false;
    Rect menuRect{};
    int  menuCount = 0;
    Rect menuItems[12]{};
    std::wstring menuTexts[12]{};
    UiAction menuActions[12]{};
    int  hoverMenuIndex = -1;

    // 交互进行中
    bool dragging = false;
    bool resizing = false;

    // 缩放角标
    Rect resizeHandle{};

    // 短暂提示
    std::wstring toast;
    float toastTimer = 0.0f;
};

// ---------------------------------------------------------------------------
// 设置项（持久化）
// ---------------------------------------------------------------------------
struct Settings {
    bool autostart = false;
    UINT hotkeyMods = MOD_CONTROL | MOD_ALT;
    UINT hotkeyVk   = 'F';
    bool clickThroughLocked = false;
    bool closeToTray = true;            // 点关闭时隐藏到托盘（后台继续养鱼）
    std::wstring weatherHost = L"wttr.in";
    std::wstring weatherPath = L"/?format=j1";
    float heaterTarget = 26.0f;
};

// ---------------------------------------------------------------------------
// 世界
// ---------------------------------------------------------------------------
struct World {
    HWND  hwnd    = nullptr;
    int   clientW = kBaseWidth;
    int   clientH = kBaseHeight;
    float scale   = 1.0f;          // 物理像素 / 设计像素
    Rect  water{};                 // 设计坐标水体

    float animClock  = 0.0f;       // 渲染用累计时间（秒），只由渲染线程推进
    float logicClock = 0.0f;

    std::vector<Fish>  fish;       // 固定 kMaxFish 个槽位
    std::vector<Plant> plants;
    Equipment equip;
    Env       env;
    UiState   ui;
    Settings  settings;

    // 环境外部输入
    float        ambientTemp = 24.0f;   // 天气基准温度
    bool         weatherOnline = false;
    std::wstring weatherNote;
    float        weatherAge = 0.0f;

    int  slotRotation = 0;              // 添加鱼时的物种轮换游标
    bool quitRequested = false;
};

// 设计坐标下的水体矩形
inline Rect MakeWaterRect() {
    return Rect::FromXYWH(kWaterInset, kWaterInset,
                          static_cast<float>(kBaseWidth) - kWaterInset * 2.0f,
                          static_cast<float>(kBaseHeight) - kWaterInset * 2.0f);
}

// 由客户区物理尺寸换算缩放系数
inline float ComputeScale(int clientW) {
    return static_cast<float>(clientW) / static_cast<float>(kBaseWidth);
}

// 客户区物理坐标 -> 设计坐标
inline Vec2 ClientToDesign(float x, float y, float scale) {
    if (scale <= 0.0f) scale = 1.0f;
    return Vec2{ x / scale, y / scale };
}

// ---------------------------------------------------------------------------
// 鱼精灵的绘制矩形（设计坐标）。命中测试与渲染共用，保证"看到哪里就能点哪里"。
// ---------------------------------------------------------------------------
inline Rect FishSpriteRect(const Fish& f, const SpritePack& pack) {
    const float w = static_cast<float>(pack.frameWidth) * pack.renderScale;
    const float h = static_cast<float>(pack.frameHeight) * pack.renderScale;
    return Rect::FromXYWH(f.pos.x - w * 0.5f, f.pos.y - h * 0.5f, w, h);
}

// 鱼的鼠标碰撞盒（设计坐标）：来自 sprite.json 的 hitbox，朝左时水平镜像
inline Rect FishHitRect(const Fish& f, const SpritePack& pack, const AnimClip& clip) {
    Rect hb = clip.hasHitbox ? clip.hitbox : pack.hitbox;
    const float s = pack.renderScale;
    const float fw = static_cast<float>(pack.frameWidth);
    const Rect sprite = FishSpriteRect(f, pack);

    float x = hb.l;
    if (f.facing < 0.0f) x = fw - hb.r;   // 水平镜像
    return Rect::FromXYWH(sprite.l + x * s, sprite.t + hb.t * s, hb.Width() * s, hb.Height() * s);
}

} // namespace ft
