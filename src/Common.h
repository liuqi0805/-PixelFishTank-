// =============================================================================
//  Common.h  --  像素风鱼缸悬浮组件：公共头文件
//  依赖：Win32 API + Direct2D 1.1 + DirectWrite + WIC
//  目标：Windows 11 x64 / Visual Studio 2022 / SDK 10.0.22621
//
//  设计约定（非常重要，改代码前先读这里）：
//   * 全部绘制都在“设计坐标系”里完成：宽 320、高 240（4:3），
//     水体区域在设计坐标中是 waterRect（默认四周 inset 6px）。
//   * 每帧渲染前把 D2D 变换设成 Scale(scale, scale)，
//     scale = 客户区物理像素宽 / 320，因此 1 设计像素 == scale 个物理像素。
//     精灵一律用 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR 采样，
//     无论放大缩小都不糊。
//   * 逻辑线程（10Hz）与渲染线程（60fps）彻底分离，共享数据用 SRWLOCK 保护。
// =============================================================================
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <cmath>
#include <cstdint>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace ft {

// -----------------------------------------------------------------------------
// 设计空间常量
// -----------------------------------------------------------------------------
constexpr int   kBaseWidth    = 320;   // 设计坐标宽
constexpr int   kBaseHeight   = 240;   // 设计坐标高
constexpr float kWaterInset   = 6.0f;  // 水体相对窗口四边的内缩
constexpr int   kMaxFish      = 2;     // 硬性要求：最多同时 2 条鱼
constexpr int   kMaxPlants    = 8;
constexpr float kDesignAspect = static_cast<float>(kBaseWidth) / static_cast<float>(kBaseHeight);

// 尺寸限制：窗口（物理像素）面积 = 屏幕面积 1/32 ~ 1/16
constexpr float kMinAreaDiv  = 32.0f;
constexpr float kMaxAreaDiv  = 16.0f;
constexpr int   kMinWinWidth = 180;    // 兜底最小像素宽（超小屏保护）

// 鱼身上的【喂食/挑逗】按钮：鼠标离开鱼身后按钮还能存在多久（秒）。
// 按钮画在鱼上方，鼠标从鱼身上移向按钮时必然先离开鱼碰撞盒；
// 没有这个“保持窗口”，按钮会在鼠标到达之前就消失，导致永远点不到。
constexpr float kFishButtonHold = 0.4f;

// -----------------------------------------------------------------------------
// 数学 / 几何
// -----------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2  operator+(Vec2 a, Vec2 b) { return Vec2{ a.x + b.x, a.y + b.y }; }
inline Vec2  operator-(Vec2 a, Vec2 b) { return Vec2{ a.x - b.x, a.y - b.y }; }
inline Vec2  operator*(Vec2 a, float s) { return Vec2{ a.x * s, a.y * s }; }
inline float Length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Clamp01(float v) { return Clampf(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct Rect {
    float l = 0.0f, t = 0.0f, r = 0.0f, b = 0.0f;

    static Rect FromXYWH(float x, float y, float w, float h) { return Rect{ x, y, x + w, y + h }; }
    static Rect FromPoints(float x0, float y0, float x1, float y1) { return Rect{ x0, y0, x1, y1 }; }

    float Width()  const { return r - l; }
    float Height() const { return b - t; }
    bool  Empty()  const { return (r - l) <= 0.0f || (b - t) <= 0.0f; }
    Vec2  Center() const { return Vec2{ (l + r) * 0.5f, (t + b) * 0.5f }; }

    bool Contains(Vec2 p) const { return p.x >= l && p.x < r && p.y >= t && p.y < b; }
    Rect Inflate(float d) const { return Rect{ l - d, t - d, r + d, b + d }; }

    // 把自己整个塞进 outer（用于菜单/按钮出屏修正）
    Rect ClampInside(const Rect& o) const {
        float w = Width(), h = Height();
        float nx = Clampf(l, o.l, (o.r - w) < o.l ? o.l : (o.r - w));
        float ny = Clampf(t, o.t, (o.b - h) < o.t ? o.t : (o.b - h));
        return Rect{ nx, ny, nx + w, ny + h };
    }
};

inline Rect Intersect(const Rect& a, const Rect& b) {
    Rect o;
    o.l = (a.l > b.l) ? a.l : b.l;
    o.t = (a.t > b.t) ? a.t : b.t;
    o.r = (a.r < b.r) ? a.r : b.r;
    o.b = (a.b < b.b) ? a.b : b.b;
    if (o.Empty()) return Rect{};
    return o;
}

inline D2D1_RECT_F ToD2D(const Rect& rc) {
    return D2D1::RectF(rc.l, rc.t, rc.r, rc.b);
}

inline D2D1_COLOR_F Rgba(int r, int g, int b, float a) {
    return D2D1::ColorF(static_cast<float>(r) / 255.0f,
                        static_cast<float>(g) / 255.0f,
                        static_cast<float>(b) / 255.0f,
                        a);
}

inline D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) {
    c.a = a;
    return c;
}

// -----------------------------------------------------------------------------
// 鱼动画类型（与 sprite.json 的 animations 键名一一对应）
// -----------------------------------------------------------------------------
enum class FishAnim {
    Idle = 0,   // idle  游动
    Feed,       // feed  喂食
    Tease,      // tease 挑逗
    Fat,        // fat   肥胖
    Weak,       // weak  虚弱
    Dead        // dead  死亡
};

inline const wchar_t* AnimName(FishAnim a) {
    switch (a) {
        case FishAnim::Idle:  return L"idle";
        case FishAnim::Feed:  return L"feed";
        case FishAnim::Tease: return L"tease";
        case FishAnim::Fat:   return L"fat";
        case FishAnim::Weak:  return L"weak";
        case FishAnim::Dead:  return L"dead";
    }
    return L"idle";
}

// -----------------------------------------------------------------------------
// SRWLOCK 的 RAII 封装
// -----------------------------------------------------------------------------
class SrwExclusive {
public:
    explicit SrwExclusive(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockExclusive(&lock_); }
    ~SrwExclusive() { ReleaseSRWLockExclusive(&lock_); }
    SrwExclusive(const SrwExclusive&) = delete;
    SrwExclusive& operator=(const SrwExclusive&) = delete;
private:
    SRWLOCK& lock_;
};

class SrwShared {
public:
    explicit SrwShared(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockShared(&lock_); }
    ~SrwShared() { ReleaseSRWLockShared(&lock_); }
    SrwShared(const SrwShared&) = delete;
    SrwShared& operator=(const SrwShared&) = delete;
private:
    SRWLOCK& lock_;
};

} // namespace ft
