#include "Renderer.h"
#include "Util.h"

namespace ft {

Renderer::~Renderer() {
    Destroy();
}

namespace {

std::wstring Hex(UINT32 v) {
    wchar_t buf[16];
    ::swprintf_s(buf, L"0x%08X", v);
    return buf;
}

} // namespace

bool Renderer::Create(HWND hwnd) {
    hwnd_ = hwnd;
    error_.clear();

    D2D1_FACTORY_OPTIONS opts{};
    HRESULT hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     __uuidof(ID2D1Factory1), &opts,
                                     reinterpret_cast<void**>(factory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        error_ = L"D2D1CreateFactory 失败 hr=" + Hex(static_cast<UINT32>(hr));
        return false;
    }

    hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(wic_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        error_ = L"WIC 工厂创建失败 hr=" + Hex(static_cast<UINT32>(hr)) +
                 L"（渲染线程是否已 CoInitializeEx？）";
        return false;
    }

    hr = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                               reinterpret_cast<IUnknown**>(dwrite_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        error_ = L"DWriteCreateFactory 失败 hr=" + Hex(static_cast<UINT32>(hr));
        return false;
    }

    screen_ = ::GetDC(nullptr);
    if (!screen_) {
        error_ = L"GetDC(nullptr) 失败";
        return false;
    }
    mem_ = ::CreateCompatibleDC(screen_);
    if (!mem_) {
        error_ = L"CreateCompatibleDC 失败";
        return false;
    }

    return true;
}

void Renderer::ReleaseSurface() {
    if (mem_ && oldBmp_) {
        ::SelectObject(mem_, oldBmp_);
        oldBmp_ = nullptr;
    }
    if (dib_) {
        ::DeleteObject(dib_);
        dib_ = nullptr;
    }
    bits_ = nullptr;
    target_.Reset();
    ready_ = false;
}

void Renderer::Destroy() {
    bitmaps_.clear();
    formats_.clear();
    ReleaseSurface();
    if (mem_) {
        ::DeleteDC(mem_);
        mem_ = nullptr;
    }
    if (screen_) {
        ::ReleaseDC(nullptr, screen_);
        screen_ = nullptr;
    }
    target_.Reset();
    dwrite_.Reset();
    wic_.Reset();
    factory_.Reset();
}

bool Renderer::EnsureSize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (ready_ && width == width_ && height == height_ && target_) return true;

    width_  = width;
    height_ = height;
    ReleaseSurface();

    // 32 位 BGRA、自顶向下 DIB：既是 UpdateLayeredWindow 的源，也是 D2D 的后备缓冲
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = width_;
    bi.bmiHeader.biHeight      = -height_;   // 负值 = top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dib_ = ::CreateDIBSection(mem_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!dib_) return false;
    oldBmp_ = ::SelectObject(mem_, dib_);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f,
        D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);

    HRESULT hr = factory_->CreateDCRenderTarget(&props, target_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        ReleaseSurface();
        return false;
    }

    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    ready_ = true;
    return true;
}

bool Renderer::Begin() {
    if (!ready_ || !target_ || inDraw_) return false;
    RECT rc{ 0, 0, width_, height_ };
    if (FAILED(target_->BindDC(mem_, &rc))) return false;
    target_->BeginDraw();
    inDraw_ = true;
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));   // 全透明底
    return true;
}

bool Renderer::End() {
    if (!inDraw_ || !target_) return false;
    inDraw_ = false;

    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseSurface();
        return false;
    }
    if (FAILED(hr)) return false;

    if (!hwnd_) return false;

    RECT wr{};
    if (!::GetWindowRect(hwnd_, &wr)) return false;

    POINT ptSrc{ 0, 0 };
    SIZE  sz{ width_, height_ };
    POINT ptDst{ wr.left, wr.top };
    BLENDFUNCTION bf{};
    bf.BlendOp             = AC_SRC_OVER;
    bf.BlendFlags          = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat         = AC_SRC_ALPHA;

    BOOL ok = ::UpdateLayeredWindow(hwnd_, screen_, &ptDst, &sz, mem_, &ptSrc, 0, &bf, ULW_ALPHA);
    return ok != FALSE;
}

bool Renderer::DecodeToBitmap(ID2D1RenderTarget* rt, const std::wstring& path,
                              ComPtr<ID2D1Bitmap>& outNormal, ComPtr<ID2D1Bitmap>& outFlipped) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand,
                                                 decoder.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> conv;
    hr = wic_->CreateFormatConverter(conv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // PBGRA = 预乘 alpha 的 BGRA，正是 D2D 需要的；不做任何缩放/抖动，像素保持原样
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                          nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0) return false;

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);

    hr = rt->CreateBitmapFromWicBitmap(conv.Get(), &props, outNormal.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    // 预生成水平镜像：逐行左右翻转像素，鱼转向时直接换位图
    const UINT stride = w * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * h);
    hr = conv->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) return true;   // 镜像失败不影响正图使用

    std::vector<uint8_t> mirrored(pixels.size());
    for (UINT y = 0; y < h; ++y) {
        const uint8_t* src = pixels.data() + static_cast<size_t>(y) * stride;
        uint8_t* dst = mirrored.data() + static_cast<size_t>(y) * stride;
        for (UINT x = 0; x < w; ++x) {
            const uint8_t* s = src + static_cast<size_t>(x) * 4;
            uint8_t* d = dst + static_cast<size_t>(w - 1 - x) * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }

    hr = rt->CreateBitmap(D2D1::SizeU(w, h), mirrored.data(), stride, props,
                          outFlipped.ReleaseAndGetAddressOf());
    return SUCCEEDED(hr);
}

ID2D1Bitmap* Renderer::GetBitmap(const std::wstring& absPath, bool flipped) {
    if (!ready_ || !target_ || absPath.empty()) return nullptr;

    auto it = bitmaps_.find(absPath);
    if (it == bitmaps_.end()) {
        CachedBitmap entry;
        if (!DecodeToBitmap(target_.Get(), absPath, entry.normal, entry.flipped)) {
            // 缓存一个空条目，避免每帧重复尝试解码不存在的文件
            it = bitmaps_.emplace(absPath, CachedBitmap{}).first;
            return nullptr;
        }
        it = bitmaps_.emplace(absPath, std::move(entry)).first;
    }

    CachedBitmap& cached = it->second;
    if (flipped && cached.flipped) return cached.flipped.Get();
    return cached.normal.Get();
}

IDWriteTextFormat* Renderer::GetFormat(float size, bool bold) {
    if (!dwrite_) return nullptr;

    wchar_t key[64];
    ::swprintf_s(key, L"%d|%d", static_cast<int>(size * 10.0f), bold ? 1 : 0);
    std::wstring k(key);

    auto it = formats_.find(k);
    if (it != formats_.end()) {
        // 共享的 format 可能被 DrawTextLeft 临时改过对齐，这里恢复居中
        it->second->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        it->second->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return it->second.Get();
    }

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = dwrite_->CreateTextFormat(
        L"Microsoft YaHei UI", nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"zh-cn", fmt.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        // 系统上没有雅黑时退回通用字体
        hr = dwrite_->CreateTextFormat(
            L"Segoe UI", nullptr,
            bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"", fmt.ReleaseAndGetAddressOf());
        if (FAILED(hr)) return nullptr;
    }

    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    IDWriteTextFormat* raw = fmt.Get();
    formats_.emplace(k, std::move(fmt));
    return raw;
}

// -----------------------------------------------------------------------------
// 绘制小工具
// -----------------------------------------------------------------------------
void FillRect(ID2D1RenderTarget* rt, const Rect& r, D2D1_COLOR_F color) {
    if (!rt || r.Empty()) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) return;
    D2D1_RECT_F d = ToD2D(r);
    rt->FillRectangle(&d, brush.Get());
}

void StrokeRect(ID2D1RenderTarget* rt, const Rect& r, float width, D2D1_COLOR_F color) {
    if (!rt || r.Empty()) return;
    D2D1_RECT_F d = ToD2D(r);
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(rt->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) {
        rt->DrawRectangle(&d, brush.Get(), width);
    }
}

void DrawPixelPanel(ID2D1RenderTarget* rt, const Rect& r,
                    D2D1_COLOR_F fill, D2D1_COLOR_F light, D2D1_COLOR_F dark) {
    if (!rt || r.Empty()) return;

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(fill, brush.ReleaseAndGetAddressOf()))) return;
    D2D1_RECT_F d = ToD2D(r);
    rt->FillRectangle(&d, brush.Get());

    // 1px 亮边：上 + 左
    brush->SetColor(light);
    D2D1_RECT_F top   = D2D1::RectF(r.l, r.t, r.r, r.t + 1.0f);
    D2D1_RECT_F left  = D2D1::RectF(r.l, r.t, r.l + 1.0f, r.b);
    rt->FillRectangle(&top, brush.Get());
    rt->FillRectangle(&left, brush.Get());

    // 1px 暗边：下 + 右
    brush->SetColor(dark);
    D2D1_RECT_F bottom = D2D1::RectF(r.l + 1.0f, r.b - 1.0f, r.r, r.b);
    D2D1_RECT_F right  = D2D1::RectF(r.r - 1.0f, r.t + 1.0f, r.r, r.b - 1.0f);
    rt->FillRectangle(&bottom, brush.Get());
    rt->FillRectangle(&right, brush.Get());
}

void DrawPixelButton(ID2D1RenderTarget* rt, const Rect& r, bool hovered, bool active,
                     const std::wstring& text, IDWriteTextFormat* fmt,
                     D2D1_COLOR_F textColor) {
    if (!rt || r.Empty()) return;

    D2D1_COLOR_F fill   = active ? Rgba(38, 92, 118, 0.95f)
                        : (hovered ? Rgba(30, 66, 92, 0.95f) : Rgba(14, 30, 46, 0.92f));
    D2D1_COLOR_F light  = hovered ? Rgba(170, 232, 255, 1.0f) : Rgba(110, 190, 220, 1.0f);
    D2D1_COLOR_F dark   = Rgba(4, 10, 18, 1.0f);

    DrawPixelPanel(rt, r, fill, light, dark);
    if (!text.empty()) {
        DrawTextIn(rt, fmt, text, r, textColor);
    }
}

void DrawTextIn(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt,
                const std::wstring& text, const Rect& r, D2D1_COLOR_F color) {
    if (!rt || !fmt || r.Empty()) return;

    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) return;
    D2D1_RECT_F d = ToD2D(r);
    rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, &d, brush.Get());
}

void DrawTextLeft(ID2D1RenderTarget* rt, IDWriteTextFormat* fmt,
                  const std::wstring& text, const Rect& r, D2D1_COLOR_F color) {
    if (!rt || !fmt || r.Empty()) return;

    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) {
        return;
    }
    D2D1_RECT_F d = ToD2D(r);
    rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, &d, brush.Get());
}

void DrawBitmapNearest(ID2D1RenderTarget* rt, ID2D1Bitmap* bmp, const Rect& dest,
                       float opacity, const Rect* srcRect) {
    if (!rt || !bmp) return;
    D2D1_RECT_F d = ToD2D(dest);
    if (srcRect) {
        D2D1_RECT_F s = ToD2D(*srcRect);
        rt->DrawBitmap(bmp, &d, opacity, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &s);
    } else {
        rt->DrawBitmap(bmp, &d, opacity, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr);
    }
}

} // namespace ft
