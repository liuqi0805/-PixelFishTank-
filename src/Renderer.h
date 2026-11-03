// =============================================================================
//  Renderer.h  --  Direct2D 1.1 渲染后端
//
//  为什么用 ID2D1DCRenderTarget + UpdateLayeredWindow？
//   需要 WS_EX_LAYERED 逐像素 alpha（透明处鼠标穿透到桌面图标）。
//   D2D 1.1 的 ID2D1DeviceContext + DXGI 交换链与 UpdateLayeredWindow 不兼容，
//   要配合 DirectComposition 才能做透明窗口；而 DCRenderTarget 直接往 32 位
//   DIB 上画，再 UpdateLayeredWindow(ULW_ALPHA) 提交，是纯 native 方案，
//   也方便稳定贴 60fps。ID2D1Factory1 + WIC 解码 + NEAREST_NEIGHBOR 采样，
//   满足"像素风不模糊"的硬性要求。
// =============================================================================
#pragma once

#include "Common.h"

namespace ft {

struct CachedBitmap {
    ComPtr<ID2D1Bitmap> normal;   // 原始朝向
    ComPtr<ID2D1Bitmap> flipped;  // 预生成的水平镜像（鱼朝左时用）
};

class Renderer {
public:
    ~Renderer();

    bool Create(HWND hwnd);
    void Destroy();

    // 客户区尺寸变化时重建 DIB / RenderTarget（尺寸没变就直接返回）
    bool EnsureSize(int width, int height);

    bool Begin();   // 开始一帧：清成全透明
    bool End();     // 结束一帧：EndDraw + UpdateLayeredWindow

    // 位图（绝对路径），失败返回 nullptr；同一路径只解码一次
    ID2D1Bitmap* GetBitmap(const std::wstring& absPath, bool flipped = false);
    IDWriteTextFormat* GetFormat(float size, bool bold = false);

    ID2D1Factory1*      factory1() const { return factory_.Get(); }
    IWICImagingFactory* wic()      const { return wic_.Get(); }
    IDWriteFactory*     dwrite()   const { return dwrite_.Get(); }
    ID2D1RenderTarget*  target()   const { return target_.Get(); }

    int  width()  const { return width_; }
    int  height() const { return height_; }
    bool ready()  const { return ready_; }

    // 创建失败时的原因（便于排查“为什么桌面上什么都没有”）
    const std::wstring& error() const { return error_; }

    void ClearBitmapCache() { bitmaps_.clear(); }

private:
    void ReleaseSurface();
    bool DecodeToBitmap(ID2D1RenderTarget* rt, const std::wstring& path,
                        ComPtr<ID2D1Bitmap>& outNormal, ComPtr<ID2D1Bitmap>& outFlipped);

    HWND    hwnd_   = nullptr;
    HDC     screen_ = nullptr;
    HDC     mem_    = nullptr;
    HBITMAP dib_    = nullptr;
    void*   bits_   = nullptr;
    HGDIOBJ oldBmp_ = nullptr;

    ComPtr<ID2D1Factory1>       factory_;
    ComPtr<IWICImagingFactory>  wic_;
    ComPtr<IDWriteFactory>      dwrite_;
    ComPtr<ID2D1DCRenderTarget> target_;

    std::wstring error_;

    int  width_  = 0;
    int  height_ = 0;
    bool ready_  = false;
    bool inDraw_ = false;

    std::unordered_map<std::wstring, CachedBitmap>              bitmaps_;
    std::unordered_map<std::wstring, ComPtr<IDWriteTextFormat>> formats_;
};

// ---------------------------------------------------------------------------
// 绘制小工具（都在设计坐标系下调用，此时 target 的变换应为 Scale(scale)）
// ---------------------------------------------------------------------------
void FillRect(ID2D1RenderTarget* rt, const Rect& r, D2D1_COLOR_F color);
void StrokeRect(ID2D1RenderTarget* rt, const Rect& r, float width, D2D1_COLOR_F color);

// 像素风面板：填充 + 1px 亮边（左上）+ 1px 暗边（右下），硬直角
void DrawPixelPanel(ID2D1RenderTarget* rt, const Rect& r,
                    D2D1_COLOR_F fill, D2D1_COLOR_F light, D2D1_COLOR_F dark);

void DrawPixelButton(ID2D1RenderTarget* rt, const Rect& r, bool hovered, bool active,
                     const std::wstring& text, IDWriteTextFormat* fmt,
                     D2D1_COLOR_F textColor);

void DrawTextIn(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt,
                const std::wstring& text, const Rect& r, D2D1_COLOR_F color);

void DrawTextLeft(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt,
                  const std::wstring& text, const Rect& r, D2D1_COLOR_F color);

// 最近邻贴图。srcRect 为空表示整张图（单位：源像素）
void DrawBitmapNearest(ID2D1RenderTarget* rt, ID2D1Bitmap* bmp, const Rect& dest,
                       float opacity, const Rect* srcRect = nullptr);

} // namespace ft
