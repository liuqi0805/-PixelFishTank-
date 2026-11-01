#include "Ui.h"
#include "Util.h"

#include <cwchar>

namespace ft {

namespace {

constexpr float kBtnFont   = 7.0f;
constexpr float kMenuFont  = 7.0f;
constexpr float kToastFont = 7.0f;

const D2D1_COLOR_F kTextColor     = Rgba(228, 246, 255, 1.0f);
const D2D1_COLOR_F kTextDim       = Rgba(170, 205, 228, 1.0f);
const D2D1_COLOR_F kPanelFill     = Rgba(12, 26, 42, 0.88f);
const D2D1_COLOR_F kPanelLight    = Rgba(120, 205, 232, 1.0f);
const D2D1_COLOR_F kPanelDark     = Rgba(4, 10, 18, 1.0f);
const D2D1_COLOR_F kBtnFill       = Rgba(22, 46, 70, 0.95f);
const D2D1_COLOR_F kBtnFillHover  = Rgba(48, 96, 132, 0.98f);
const D2D1_COLOR_F kBtnFillActive = Rgba(28, 92, 76, 0.98f);
const D2D1_COLOR_F kBtnLight      = Rgba(140, 215, 240, 1.0f);
const D2D1_COLOR_F kBtnDark       = Rgba(3, 8, 14, 1.0f);
const D2D1_COLOR_F kHandleColor   = Rgba(190, 235, 250, 0.85f);

const AnimClip* ClipFor(const SpritePack& pack, FishAnim a) {
    const AnimClip* c = nullptr;
    for (const auto& clip : pack.clips) {
        if (clip.name == AnimName(a)) {
            c = &clip;
            break;
        }
    }
    if (!c) c = pack.FindClip(L"idle");
    if (!c) return nullptr;
    return c;
}

const AnimClip* FishClip(const SpritePack& pack, const Fish& f) {
    return ClipFor(pack, f.anim);
}

int CountUsedFish(const World& w) {
    int n = 0;
    for (const auto& f : w.fish) {
        if (f.used) ++n;
    }
    return n;
}

} // namespace

// -----------------------------------------------------------------------------
//  布局
// -----------------------------------------------------------------------------
Rect Ui::FishButtonsRect(const World& w, int fishIndex, const ResourceManager& res) const {
    if (fishIndex < 0 || fishIndex >= static_cast<int>(w.fish.size())) return Rect{};
    const Fish& f = w.fish[fishIndex];
    if (!f.used) return Rect{};
    const SpritePack* pack = res.FindFish(f.packName);
    if (!pack) return Rect{};

    const AnimClip* clip = FishClip(*pack, f);
    const Rect hit = clip ? FishHitRect(f, *pack, *clip) : FishSpriteRect(f, *pack);

    const float gap   = 3.0f;
    const float total = btnW_ * 2.0f + gap;
    float x = Clampf(hit.Center().x - total * 0.5f,
                     w.water.l + 2.0f,
                     (w.water.r - total - 2.0f < w.water.l + 2.0f) ? w.water.l + 2.0f
                                                                  : w.water.r - total - 2.0f);
    float y = hit.t - btnH_ - 2.0f;
    if (y < w.water.t + 2.0f) y = hit.b + 2.0f;
    y = Clampf(y, w.water.t + 1.0f, (w.water.b - btnH_ - 1.0f < w.water.t) ? w.water.t
                                                                          : w.water.b - btnH_ - 1.0f);
    return Rect::FromXYWH(x, y, total, btnH_);
}

void Ui::UpdateLayout(World& w, const ResourceManager& res) {
    const float now = w.animClock;
    w.ui.resizeHandle = Rect{};   // 固定尺寸：不再有缩放角标

    // ---- 常驻菜单入口（右上角，永远可见） ----
    const float mb = 11.0f;
    w.ui.menuButton = Rect::FromXYWH(w.water.r - mb - 5.0f, w.water.t + 6.0f, mb, mb);

    // ---- 鼠标是否压在鱼身上 ----
    int hovered = -1;
    for (int i = 0; i < static_cast<int>(w.fish.size()); ++i) {
        const Fish& f = w.fish[i];
        if (!f.used || !f.alive) continue;
        const SpritePack* pack = res.FindFish(f.packName);
        if (!pack) continue;
        const AnimClip* clip = FishClip(*pack, f);
        const Rect box = clip ? FishHitRect(f, *pack, *clip) : FishSpriteRect(f, *pack);
        if (box.Contains(w.ui.mouse)) { hovered = i; break; }
    }
    w.ui.hoverFish = hovered;

    // ---- 鱼身上的按钮 + 0.4s 保持 ----
    // 鼠标压在鱼身上，或者压在按钮行里，都把这个“保持时刻”往后推；
    // 只有两者都不满足且超时才真正隐藏，这样从鱼移向按钮的那一小段空白不会断。
    if (hovered >= 0) {
        w.ui.buttonOwner = hovered;
        w.ui.buttonHold  = now + kFishButtonHold;
    } else if (w.ui.buttonOwner >= 0) {
        const Rect all = FishButtonsRect(w, w.ui.buttonOwner, res);
        if (!all.Empty() && all.Inflate(1.0f).Contains(w.ui.mouse)) {
            w.ui.buttonHold = now + kFishButtonHold;
        }
    }

    // 归零检查：超时、鱼没了/死了/资源缺失，都收回按钮
    if (w.ui.buttonOwner >= 0) {
        const Fish& f = w.fish[w.ui.buttonOwner];
        const SpritePack* pack = res.FindFish(f.packName);
        if (w.ui.buttonHold <= now || !f.used || !f.alive || !pack) w.ui.buttonOwner = -1;
    }

    w.ui.feedBtn  = Rect{};
    w.ui.teaseBtn = Rect{};
    w.ui.fishButtonsVisible = false;
    if (w.ui.buttonOwner >= 0) {
        const Rect all = FishButtonsRect(w, w.ui.buttonOwner, res);
        if (!all.Empty()) {
            w.ui.feedBtn  = Rect::FromXYWH(all.l, all.t, btnW_, btnH_);
            w.ui.teaseBtn = Rect::FromXYWH(all.l + btnW_ + 3.0f, all.t, btnW_, btnH_);
            w.ui.fishButtonsVisible = true;
        }
    }

    // ---- 水体空白处的操作面板 ----
    // 鼠标压在按钮行上时也要保持面板不出现，否则面板会盖住鱼按钮。
    const bool onButtons = w.ui.fishButtonsVisible &&
                           (w.ui.feedBtn.Inflate(1.0f).Contains(w.ui.mouse) ||
                            w.ui.teaseBtn.Inflate(1.0f).Contains(w.ui.mouse));
    // 注意：这里不能再要求 buttonOwner < 0 —— 按钮的 0.4s 保持窗会让“刚喂完鱼就把鼠标挪到
    // 水面上”的面板延迟出现；面板固定在左上角，和按钮行本来就不会打架。
    w.ui.panelVisible = (!w.ui.menuOpen && !w.ui.dragging && !w.ui.resizing && w.ui.mouseInside &&
                         !onButtons && w.ui.hoverFish < 0 &&
                         !w.ui.menuButton.Contains(w.ui.mouse) &&
                         w.water.Contains(w.ui.mouse));

    const float pad = 4.0f;
    const float bh  = 13.0f;
    const float bw  = 92.0f;
    const float panelW = bw + pad * 2.0f;
    const float panelH = pad * 2.0f + bh * 2.0f + 3.0f;
    float px = w.water.l + 6.0f;
    float py = w.water.t + 6.0f;
    if (px + panelW > w.water.r - 2.0f) px = w.water.r - panelW - 2.0f;
    w.ui.panelRect  = Rect::FromXYWH(px, py, panelW, panelH);
    w.ui.aeratorBtn = Rect::FromXYWH(px + pad, py + pad, bw, bh);
    w.ui.heaterBtn  = Rect::FromXYWH(px + pad, py + pad + bh + 3.0f, bw, bh);
}

// -----------------------------------------------------------------------------
//  命中测试
// -----------------------------------------------------------------------------
HitResult Ui::HitTest(const World& w, Vec2 p, const ResourceManager& res) const {
    HitResult h;

    // 1) 右键菜单（最顶层）
    if (w.ui.menuOpen && w.ui.menuRect.Contains(p)) {
        h.menuOpen = true;
        for (int i = 0; i < w.ui.menuCount; ++i) {
            if (w.ui.menuItems[i].Contains(p)) {
                h.menuIndex = i;
                break;
            }
        }
        return h;   // 菜单打开时不再向下穿透
    }
    if (w.ui.menuOpen) h.menuOpen = true;

    // 2) 常驻菜单入口（右上角 ☰）
    if (w.ui.menuButton.Contains(p)) {
        h.menuButton = true;
        return h;
    }

    // 4) 鱼身上的按钮（按“归属鱼”判定，不依赖当前是否还悬停在鱼身上）
    if (w.ui.fishButtonsVisible && w.ui.buttonOwner >= 0) {
        UiAction btn = UiAction::None;
        if (w.ui.feedBtn.Contains(p)) {
            btn = UiAction::Feed;
            h.fishButtonRect = w.ui.feedBtn;
        } else if (w.ui.teaseBtn.Contains(p)) {
            btn = UiAction::Tease;
            h.fishButtonRect = w.ui.teaseBtn;
        }
        if (btn != UiAction::None) {
            const int owner = w.ui.buttonOwner;
            h.fishButton = btn;
            h.fish.hit = true;
            h.fish.index = owner;
            if (owner >= 0 && owner < static_cast<int>(w.fish.size())) {
                if (const SpritePack* pack = res.FindFish(w.fish[owner].packName)) {
                    const AnimClip* clip = FishClip(*pack, w.fish[owner]);
                    h.fish.rect = clip ? FishHitRect(w.fish[owner], *pack, *clip)
                                       : FishSpriteRect(w.fish[owner], *pack);
                }
            }
            return h;
        }
    }

    // 5) 鱼碰撞盒（来自 sprite.json 的 hitbox，优先于面板）
    for (int i = 0; i < static_cast<int>(w.fish.size()); ++i) {
        const Fish& f = w.fish[i];
        if (!f.used) continue;
        const SpritePack* pack = res.FindFish(f.packName);
        if (!pack) continue;
        const AnimClip* clip = FishClip(*pack, f);
        const Rect box = clip ? FishHitRect(f, *pack, *clip) : FishSpriteRect(f, *pack);
        if (box.Contains(p)) {
            h.fish.hit = true;
            h.fish.index = i;
            h.fish.rect = box;
            return h;
        }
    }

    // 6) 水体操作面板
    if (w.ui.panelVisible) {
        if (w.ui.aeratorBtn.Contains(p)) {
            h.panelButton = UiAction::ToggleAerator;
            h.panelButtonRect = w.ui.aeratorBtn;
            return h;
        }
        if (w.ui.heaterBtn.Contains(p)) {
            h.panelButton = UiAction::ToggleHeater;
            h.panelButtonRect = w.ui.heaterBtn;
            return h;
        }
    }

    // 7) 水体空白区域（可拖动窗口）
    h.inWater = w.water.Contains(p);
    return h;
}

void Ui::ApplyHit(World& w, const HitResult& hit) {
    // hoverFish 由 UpdateLayout 按鼠标位置统一维护，这里不再覆写，
    // 否则鼠标停在“按钮”（已离开鱼身）时会被错误地写成悬停鱼。
    if (hit.fishButton != UiAction::None) {
        w.ui.hoverAction = static_cast<int>(hit.fishButton);
    } else if (hit.panelButton != UiAction::None) {
        w.ui.hoverAction = static_cast<int>(hit.panelButton);
    } else {
        w.ui.hoverAction = -1;
    }
    w.ui.hoverMenuIndex = hit.menuIndex;
}

// -----------------------------------------------------------------------------
//  右键菜单
// -----------------------------------------------------------------------------
void Ui::BuildMenu(World& w, Vec2 pt) {
    w.ui.menuCount = 0;

    auto add = [&w](const std::wstring& text, UiAction action) {
        if (w.ui.menuCount >= 12) return;
        const int i = w.ui.menuCount++;
        w.ui.menuTexts[i]   = text;
        w.ui.menuActions[i] = action;
        w.ui.menuItems[i]   = Rect{};
    };

    add(L"添加水草", UiAction::AddPlant);
    add(L"添加鱼", UiAction::AddFish);
    add(L"清空鱼缸", UiAction::ClearTank);
    add(L"重置环境", UiAction::ResetEnv);
    add(w.settings.clickThroughLocked ? L"锁定鼠标穿透：开" : L"锁定鼠标穿透：关",
        UiAction::ToggleClickThrough);
    add(w.settings.autostart ? L"开机自启：开" : L"开机自启：关", UiAction::ToggleAutostart);
    add(w.settings.closeToTray ? L"关闭到托盘：开" : L"关闭到托盘：关", UiAction::ToggleCloseToTray);
    add(L"退出程序", UiAction::Quit);

    const float itemH = 13.0f;
    const float pad   = 4.0f;
    const float width = 110.0f;
    const float height = pad * 2.0f + itemH * static_cast<float>(w.ui.menuCount);

    // 以鼠标点为左上角，越界就往回收
    const Rect bounds = Rect::FromXYWH(0.0f, 0.0f,
                                       static_cast<float>(kBaseWidth),
                                       static_cast<float>(kBaseHeight)).Inflate(-1.0f);
    Rect menu = Rect::FromXYWH(pt.x, pt.y, width, height).ClampInside(bounds);
    w.ui.menuRect = menu;

    for (int i = 0; i < w.ui.menuCount; ++i) {
        w.ui.menuItems[i] = Rect::FromXYWH(menu.l + 2.0f, menu.t + pad + itemH * static_cast<float>(i),
                                           width - 4.0f, itemH);
    }
    w.ui.menuOpen = true;
    w.ui.hoverMenuIndex = -1;
    w.ui.panelVisible = false;
    w.ui.fishButtonsVisible = false;
}

void Ui::CloseMenu(World& w) {
    w.ui.menuOpen = false;
    w.ui.hoverMenuIndex = -1;
    w.ui.menuCount = 0;
    for (int i = 0; i < 12; ++i) {
        w.ui.menuItems[i] = Rect{};
        w.ui.menuActions[i] = UiAction::None;
        w.ui.menuTexts[i].clear();
    }
}

// -----------------------------------------------------------------------------
//  绘制
// -----------------------------------------------------------------------------
void Ui::Render(const World& w, Renderer& r, const ResourceManager& res) {
    ID2D1RenderTarget* rt = r.target();
    if (!rt) return;

    rt->SetTransform(D2D1::Matrix3x2F::Scale(w.scale, w.scale));

    // 渲染顺序（硬性要求）：水体底色 -> 水草景观 -> 鱼精灵 -> 交互 UI
    DrawWater(w, r);
    DrawPlants(w, r, res);
    DrawEquipment(w, r, res);
    DrawFish(w, r, res);
    DrawFishButtons(w, r);
    DrawPanel(w, r, res);
    DrawMenuButton(w, r);
    DrawHud(w, r);
    DrawMenu(w, r);
    DrawToast(w, r);

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Ui::DrawWater(const World& w, Renderer& r) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt || w.water.Empty()) return;
    const Rect& wa = w.water;

    // 半透明水体：整体压暗偏蓝，保证桌面仍能隐约透出但不抢眼
    FillRect(rt, wa, Rgba(18, 62, 110, 0.62f));
    FillRect(rt, Rect::FromXYWH(wa.l, wa.Center().y, wa.Width(), wa.Height() * 0.5f),
             Rgba(6, 26, 48, 0.22f));
    FillRect(rt, Rect::FromXYWH(wa.l, wa.t, wa.Width(), 10.0f), Rgba(120, 205, 240, 0.14f));
    FillRect(rt, Rect::FromXYWH(wa.l, wa.t, wa.Width(), 2.0f), Rgba(170, 235, 252, 0.22f));

    // 底部沙层
    FillRect(rt, Rect::FromXYWH(wa.l, wa.b - 6.0f, wa.Width(), 6.0f), Rgba(212, 190, 126, 0.50f));
    FillRect(rt, Rect::FromXYWH(wa.l, wa.b - 6.0f, wa.Width(), 1.0f), Rgba(240, 222, 170, 0.55f));

    // 水面波纹
    const float t = w.animClock;
    for (int i = 0; i < 14; ++i) {
        const float x = wa.l + 4.0f + static_cast<float>(i) * (wa.Width() - 8.0f) / 14.0f;
        const float off = static_cast<float>(std::sin(static_cast<double>(t) * 1.6 + i * 0.7)) * 1.2f;
        FillRect(rt, Rect::FromXYWH(x, wa.t + 3.0f + off, 6.0f, 1.0f), Rgba(200, 245, 255, 0.30f));
    }

    // 增氧器气泡
    if (w.equip.aerator) {
        for (int i = 0; i < 6; ++i) {
            const float phase = std::fmod(t * 0.35f + static_cast<float>(i) * 0.17f, 1.0f);
            const float y = wa.b - 10.0f - phase * (wa.Height() - 20.0f);
            const float x = w.equip.aeratorPos.x + ((i % 2) ? 2.0f : -2.0f) +
                            static_cast<float>(std::sin(static_cast<double>(t) * 2.0 + i)) * 1.0f;
            const float s = (i % 3 == 0) ? 2.0f : 1.0f;
            FillRect(rt, Rect::FromXYWH(x, y, s, s), Rgba(210, 245, 255, 0.50f));
        }
    }

    StrokeRect(rt, wa, 1.0f, Rgba(150, 222, 250, 0.45f));

    if (CountUsedFish(w) == 0) {
        IDWriteTextFormat* fmt = r.GetFormat(kToastFont, false);
        DrawTextIn(rt, fmt, L"右键添加鱼", Rect::FromXYWH(wa.l, wa.Center().y - 6.0f, wa.Width(), 12.0f),
                   kTextDim);
    }
}

void Ui::DrawPlants(const World& w, Renderer& r, const ResourceManager& res) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt) return;
    for (const auto& p : w.plants) {
        const SpritePack* pack = res.FindPlant(p.packName);
        if (!pack || pack->clips.empty()) continue;
        const AnimClip* clip = pack->FindClip(L"idle");
        if (!clip) clip = &pack->clips.front();
        if (clip->frames.empty()) continue;

        const AnimFrame& frame = clip->frames[clip->FrameAt(w.animClock + p.swayPhase)];
        ID2D1Bitmap* bmp = r.GetBitmap(pack->FramePath(frame), false);
        if (!bmp) continue;

        const float sw = static_cast<float>(pack->frameWidth) * pack->renderScale * p.scale;
        const float sh = static_cast<float>(pack->frameHeight) * pack->renderScale * p.scale;
        const Rect dest = Rect::FromXYWH(p.pos.x - sw * 0.5f, p.pos.y - sh, sw, sh);   // 以底边中心为锚点
        DrawBitmapNearest(rt, bmp, dest, 1.0f);
    }
}

void Ui::DrawEquipment(const World& w, Renderer& r, const ResourceManager& res) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt) return;

    auto drawAt = [&](const wchar_t* assetName, Vec2 pos) {
        const SpritePack* pack = res.FindUi(assetName);
        if (!pack || pack->clips.empty()) return;
        const AnimClip* clip = pack->FindClip(L"idle");
        if (!clip) clip = &pack->clips.front();
        if (clip->frames.empty()) return;
        ID2D1Bitmap* bmp = r.GetBitmap(pack->FramePath(clip->frames[clip->FrameAt(w.animClock)]), false);
        if (!bmp) return;
        const float sw = static_cast<float>(pack->frameWidth) * pack->renderScale;
        const float sh = static_cast<float>(pack->frameHeight) * pack->renderScale;
        DrawBitmapNearest(rt, bmp, Rect::FromXYWH(pos.x - sw * 0.5f, pos.y - sh * 0.5f, sw, sh), 1.0f);
    };

    if (w.equip.aerator) drawAt(L"aerator", w.equip.aeratorPos);
    if (w.equip.heater)  drawAt(L"heater",  w.equip.heaterPos);
}

void Ui::DrawFish(const World& w, Renderer& r, const ResourceManager& res) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt) return;

    for (const auto& f : w.fish) {
        if (!f.used) continue;
        const SpritePack* pack = res.FindFish(f.packName);
        if (!pack) continue;
        const AnimClip* clip = FishClip(*pack, f);
        if (!clip || clip->frames.empty()) continue;

        const AnimFrame& frame = clip->frames[clip->FrameAt(f.animTime)];
        ID2D1Bitmap* bmp = r.GetBitmap(pack->FramePath(frame), f.facing < 0.0f);
        if (!bmp) continue;

        Rect dest = FishSpriteRect(f, *pack);
        const float bob = static_cast<float>(std::sin(static_cast<double>(w.animClock) * 2.0 + f.bobPhase)) * 0.8f;
        dest.t += bob;
        dest.b += bob;
        DrawBitmapNearest(rt, bmp, dest, f.alive ? 1.0f : 0.85f);
    }
}

void Ui::DrawFishButtons(const World& w, Renderer& r) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt || !w.ui.fishButtonsVisible) return;

    IDWriteTextFormat* fmt = r.GetFormat(kBtnFont, false);
    const bool feedHover  = w.ui.hoverAction == static_cast<int>(UiAction::Feed);
    const bool teaseHover = w.ui.hoverAction == static_cast<int>(UiAction::Tease);

    DrawPixelButton(rt, w.ui.feedBtn, feedHover, false, L"喂食", fmt, kTextColor);
    DrawPixelButton(rt, w.ui.teaseBtn, teaseHover, false, L"挑逗", fmt, kTextColor);
}

void Ui::DrawPanel(const World& w, Renderer& r, const ResourceManager& res) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt || !w.ui.panelVisible) return;

    IDWriteTextFormat* fmt = r.GetFormat(kBtnFont, false);
    DrawPixelPanel(rt, w.ui.panelRect, kPanelFill, kPanelLight, kPanelDark);

    // 按钮右侧的小图标（来自 assets/ui，缺失就不画）
    auto drawIcon = [&](const wchar_t* name, const Rect& btn) {
        const SpritePack* pack = res.FindUi(name);
        if (!pack || pack->clips.empty()) return;
        const AnimClip* clip = pack->FindClip(L"idle");
        if (!clip) clip = &pack->clips.front();
        if (clip->frames.empty()) return;
        ID2D1Bitmap* bmp = r.GetBitmap(pack->FramePath(clip->frames[clip->FrameAt(w.animClock)]), false);
        if (!bmp) return;
        const float s = 10.0f;
        DrawBitmapNearest(rt, bmp,
                          Rect::FromXYWH(btn.r - s - 3.0f, btn.Center().y - s * 0.5f, s, s), 1.0f);
    };

    // 增氧器
    {
        wchar_t buf[64] = { 0 };
        swprintf_s(buf, L"增氧器 %s", w.equip.aerator ? L"开" : L"关");
        const bool hover = w.ui.hoverAction == static_cast<int>(UiAction::ToggleAerator);
        DrawPixelButton(rt, w.ui.aeratorBtn, hover, w.equip.aerator, buf, fmt, kTextColor);
        drawIcon(L"aerator", w.ui.aeratorBtn);
    }
    // 加温棒
    {
        wchar_t buf[64] = { 0 };
        swprintf_s(buf, L"加温棒 %s", w.equip.heater ? L"开" : L"关");
        const bool hover = w.ui.hoverAction == static_cast<int>(UiAction::ToggleHeater);
        DrawPixelButton(rt, w.ui.heaterBtn, hover, w.equip.heater, buf, fmt, kTextColor);
        drawIcon(L"heater", w.ui.heaterBtn);
    }
}

void Ui::DrawMenuButton(const World& w, Renderer& r) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt || w.ui.menuButton.Empty()) return;

    const Rect b = w.ui.menuButton;
    const bool hovered = b.Contains(w.ui.mouse);
    const bool open    = w.ui.menuOpen;

    DrawPixelPanel(rt, b,
                   open ? Rgba(34, 74, 102, 0.95f) : Rgba(12, 28, 44, 0.85f),
                   hovered ? Rgba(175, 236, 255, 1.0f) : Rgba(108, 188, 218, 0.9f),
                   kPanelDark);

    // ☰ 三条横线（像素画法，不受字体影响）
    const D2D1_COLOR_F ink = hovered || open ? Rgba(255, 255, 255, 1.0f) : Rgba(205, 238, 252, 0.95f);
    for (int i = 0; i < 3; ++i) {
        FillRect(rt, Rect::FromXYWH(b.l + 2.5f, b.t + 3.0f + static_cast<float>(i) * 2.5f,
                                    b.Width() - 5.0f, 1.0f), ink);
    }
}

void Ui::DrawHud(const World& w, Renderer& r) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt || w.water.Empty()) return;

    // 带一位小数的水温 + 整数溶氧；离线性自动切到模拟曲线时前面加 ~
    wchar_t buf[96];
    swprintf_s(buf, L"%s%.1f°C    溶氧 %d%%",
               w.weatherOnline ? L"" : L"~",
               static_cast<double>(w.env.waterTemp),
               static_cast<int>(std::lround(Clamp01(w.env.oxygen / 100.0f) * 100.0f)));

    IDWriteTextFormat* fmt = r.GetFormat(8.0f, false);
    const float tw = Clampf(104.0f, 60.0f, w.water.Width() - 16.0f);
    const float th = 10.0f;

    // 常驻在右上角菜单入口下方，避开左上操作面板、右下缩放角标与底部设备
    const Rect box = Rect::FromXYWH(w.water.r - tw - 5.0f, w.water.t + 20.0f, tw, th);

    // 先画一层半透明黑影，亮壁纸 / 深壁纸都能读得清
    DrawTextIn(rt, fmt, buf, Rect::FromXYWH(box.l + 0.7f, box.t + 0.7f, box.Width(), box.Height()),
               Rgba(0, 0, 0, 0.45f));
    DrawTextIn(rt, fmt, buf, box, Rgba(255, 255, 255, 0.58f));
}

void Ui::DrawMenu(const World& w, Renderer& r) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt || !w.ui.menuOpen) return;

    IDWriteTextFormat* fmt = r.GetFormat(kMenuFont, false);
    DrawPixelPanel(rt, w.ui.menuRect, Rgba(10, 20, 34, 0.94f), kPanelLight, kPanelDark);

    for (int i = 0; i < w.ui.menuCount; ++i) {
        const Rect& item = w.ui.menuItems[i];
        const bool hover = (i == w.ui.hoverMenuIndex);
        if (hover) {
            FillRect(rt, item, Rgba(46, 88, 124, 0.95f));
            FillRect(rt, Rect::FromXYWH(item.l, item.t, item.Width(), 1.0f), Rgba(150, 220, 245, 0.9f));
        }
        DrawTextLeft(rt, fmt, w.ui.menuTexts[i], item, hover ? kTextColor : Rgba(210, 232, 246, 1.0f));
    }
}


void Ui::DrawToast(const World& w, Renderer& r) const {
    ID2D1RenderTarget* rt = r.target();
    if (!rt || w.ui.toast.empty() || w.ui.toastTimer <= 0.0f) return;

    IDWriteTextFormat* fmt = r.GetFormat(kToastFont, false);
    const float chars = static_cast<float>(w.ui.toast.size());
    const float tw = Clampf(chars * 7.0f + 12.0f, 40.0f, w.water.Width() - 20.0f);
    const float th = 15.0f;
    const Rect box = Rect::FromXYWH(w.water.Center().x - tw * 0.5f, w.water.b - th - 8.0f, tw, th);
    const float alpha = Clamp01(w.ui.toastTimer / 0.6f);
    DrawPixelPanel(rt, box, WithAlpha(Rgba(8, 18, 30, 1.0f), 0.9f * alpha),
                   WithAlpha(kPanelLight, 0.8f * alpha), WithAlpha(kPanelDark, 0.8f * alpha));
    DrawTextIn(rt, fmt, w.ui.toast, box, WithAlpha(kTextColor, alpha));
}

} // namespace ft
