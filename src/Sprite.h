// =============================================================================
//  Sprite.h  --  动画包（PNG 序列 + sprite.json）的数据模型与加载
//
//  资源目录约定：
//    assets/fish/<packName>/  sprite.json + 一堆 PNG
//    assets/plant/<packName>/ sprite.json + 一堆 PNG
//    assets/ui/<packName>/    sprite.json + 一堆 PNG
//
//  sprite.json 结构见 docs/assets资源目录说明.md，这里是它的内存映像。
// =============================================================================
#pragma once

#include "Common.h"

namespace ft {

struct AnimFrame {
    std::wstring file;        // 相对 pack 目录的文件名，如 L"idle_00.png"
    float        duration = 0.12f;  // 该帧停留秒数
};

struct AnimClip {
    std::wstring           name;      // idle / feed / tease / fat / weak / dead
    std::vector<AnimFrame> frames;
    bool                   loop = true;
    bool                   hasHitbox = false;
    Rect                   hitbox;    // 源像素坐标，相对帧左上角
    float                  totalDuration = 0.0f;

    // 按时间取帧号；非循环动画到末尾就停在最后一帧
    int FrameAt(float time) const;
};

struct SpritePack {
    std::wstring name;          // 资源名（目录名，如 "goldfish"）
    std::wstring displayName;   // 显示名（"金鱼"），用于菜单
    std::wstring dirPath;       // 绝对目录（带结尾反斜杠）
    std::wstring category;      // fish / plant / ui
    int          frameWidth = 0;
    int          frameHeight = 0;
    float        renderScale = 1.0f;  // 设计空间里的额外缩放（相对源像素 1:1）
    bool         hasHitbox = false;
    Rect         hitbox{};            // 默认鼠标碰撞盒（源像素）
    std::vector<AnimClip> clips;

    const AnimClip* FindClip(const wchar_t* animName) const;
    std::wstring    FramePath(const AnimFrame& f) const;
};

// 解析 <dir>\sprite.json 并填充 pack；失败返回 false 并给出原因
bool LoadSpritePack(const std::wstring& dir, const std::wstring& category,
                    SpritePack& out, std::wstring& error);

} // namespace ft
