// =============================================================================
//  Ui.h  --  像素 UI 布局 / 命中测试 / 绘制
//
//  交互优先级（硬性要求）：右键菜单 > 缩放角标 > 鱼身上的按钮 > 鱼碰撞盒 >
//                          水体操作面板 > 透明穿透
//  UI 位置全部用“设计坐标”计算，与渲染缩放无关。
// =============================================================================
#pragma once

#include "Common.h"
#include "Model.h"
#include "Renderer.h"
#include "ResourceManager.h"

namespace ft {

struct FishHit {
    bool hit = false;
    int  index = -1;
    Rect rect{};
};

struct HitResult {
    // 右键菜单
    bool     menuOpen = false;
    int      menuIndex = -1;
    // 常驻菜单入口（右上角 ☰，不需要悬停也一直在）
    bool     menuButton = false;
    // 右下角缩放角标
    bool     resizeHandle = false;   // 是否抓到缩放区
    // 鱼身上的按钮
    UiAction fishButton = UiAction::None;
    Rect     fishButtonRect{};
    // 鱼碰撞盒
    FishHit  fish;
    // 水体操作面板
    UiAction panelButton = UiAction::None;
    Rect     panelButtonRect{};
    // 水体空白区域
    bool     inWater = false;

    bool AnyUi() const {
        return menuIndex >= 0 || menuButton || resizeHandle || fishButton != UiAction::None ||
               fish.hit || panelButton != UiAction::None;
    }
};

class Ui {
public:
    // 根据当前鼠标 / 悬停状态重新计算按钮面板位置与可见性
    void UpdateLayout(World& w, const ResourceManager& res);

    // 命中测试（designPt 为设计坐标）
    HitResult HitTest(const World& w, Vec2 designPt, const ResourceManager& res) const;

    // 把命中结果写回 UI 悬停状态
    void ApplyHit(World& w, const HitResult& hit);

    // 右键菜单
    void BuildMenu(World& w, Vec2 designPt);
    void CloseMenu(World& w);

    // 悬停鱼时的参数面板矩形（设计坐标；只显示，不参与命中）
    Rect FishStatsRect(const World& w, int fishIndex, const ResourceManager& res) const;

    // 一条鱼的按钮整体矩形
    Rect FishButtonsRect(const World& w, int fishIndex, const ResourceManager& res) const;

    // 绘制（调用前 target 尚未设置变换，这里自己 SetTransform）
    void Render(const World& w, Renderer& r, const ResourceManager& res);

private:
    void DrawWater(const World& w, Renderer& r) const;
    void DrawPlants(const World& w, Renderer& r, const ResourceManager& res) const;
    void DrawEquipment(const World& w, Renderer& r, const ResourceManager& res) const;
    void DrawFish(const World& w, Renderer& r, const ResourceManager& res) const;
    void DrawFishButtons(const World& w, Renderer& r) const;
    void DrawFishStats(const World& w, Renderer& r) const;   // 鱼参数列表
    void DrawPanel(const World& w, Renderer& r, const ResourceManager& res) const;
    void DrawMenuButton(const World& w, Renderer& r) const;   // 常驻 ☰ 入口
    void DrawHud(const World& w, Renderer& r) const;          // 常驻水温 / 溶氧
    void DrawMenu(const World& w, Renderer& r) const;
    void DrawToast(const World& w, Renderer& r) const;

    // 鱼按钮尺寸（设计坐标）
    float btnW_ = 34.0f;
    float btnH_ = 12.0f;
};

} // namespace ft
