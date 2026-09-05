#include "IconLoader.h"

#include <shellapi.h>
#include <shlobj.h>

#include <cwctype>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace winmix::app {

namespace {

// GetIconInfo/GetDIBits give straight (non-premultiplied) alpha; D2D wants
// premultiplied.
void PremultiplyInPlace(std::vector<uint32_t>& pixels)
{
    for (uint32_t& p : pixels)
    {
        const BYTE a = static_cast<BYTE>(p >> 24);
        const BYTE r = static_cast<BYTE>((p >> 16) & 0xFF);
        const BYTE g = static_cast<BYTE>((p >> 8) & 0xFF);
        const BYTE b = static_cast<BYTE>(p & 0xFF);
        const BYTE pr = static_cast<BYTE>(r * a / 255);
        const BYTE pg = static_cast<BYTE>(g * a / 255);
        const BYTE pb = static_cast<BYTE>(b * a / 255);
        p = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(pr) << 16) |
            (static_cast<uint32_t>(pg) << 8) | pb;
    }
}

} // namespace

ID2D1Bitmap* IconLoader::ForExecutable(ID2D1DeviceContext* ctx, const std::optional<std::wstring>& executablePath, bool isSystemSounds)
{
    if (isSystemSounds)
    {
        if (!systemSoundsLoaded_)
        {
            systemSoundsLoaded_ = true;
            SHSTOCKICONINFO info{};
            info.cbSize = sizeof(info);
            if (SUCCEEDED(SHGetStockIconInfo(SIID_INFO, SHGSI_ICON, &info)))
            {
                systemSoundsIcon_ = FromHIcon(ctx, info.hIcon);
                DestroyIcon(info.hIcon);
            }
        }
        return systemSoundsIcon_.Get();
    }

    if (!executablePath || executablePath->empty())
    {
        return nullptr;
    }

    std::wstring key = *executablePath;
    for (wchar_t& c : key)
    {
        c = static_cast<wchar_t>(towlower(c));
    }

    if (auto it = cache_.find(key); it != cache_.end())
    {
        return it->second.Get(); // may legitimately be a cached nullptr
    }

    auto bitmap = Load(ctx, *executablePath);
    ID2D1Bitmap* result = bitmap.Get();
    cache_.emplace(std::move(key), std::move(bitmap));
    return result;
}

ComPtr<ID2D1Bitmap> IconLoader::Load(ID2D1DeviceContext* ctx, const std::wstring& path)
{
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON))
    {
        return nullptr;
    }

    auto bitmap = FromHIcon(ctx, info.hIcon);
    DestroyIcon(info.hIcon);
    return bitmap;
}

ComPtr<ID2D1Bitmap> IconLoader::FromHIcon(ID2D1DeviceContext* ctx, HICON icon)
{
    if (!icon)
    {
        return nullptr;
    }

    ICONINFO iconInfo{};
    if (!GetIconInfo(icon, &iconInfo))
    {
        return nullptr;
    }

    struct BitmapGuard
    {
        HBITMAP bmp;
        ~BitmapGuard()
        {
            if (bmp)
            {
                DeleteObject(bmp);
            }
        }
    } colorGuard{iconInfo.hbmColor}, maskGuard{iconInfo.hbmMask};

    if (!iconInfo.hbmColor)
    {
        // Legacy mono-only icon (essentially never seen from a real .exe's
        // shell icon today) -- not worth a second code path, degrade to no icon.
        return nullptr;
    }

    BITMAP bmpInfo{};
    if (!GetObject(iconInfo.hbmColor, sizeof(bmpInfo), &bmpInfo))
    {
        return nullptr;
    }

    const int width = bmpInfo.bmWidth;
    const int height = bmpInfo.bmHeight;
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    BITMAPINFO dibInfo{};
    dibInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dibInfo.bmiHeader.biWidth = width;
    dibInfo.bmiHeader.biHeight = -height; // negative = top-down
    dibInfo.bmiHeader.biPlanes = 1;
    dibInfo.bmiHeader.biBitCount = 32;
    dibInfo.bmiHeader.biCompression = BI_RGB;

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    const int scanLines = GetDIBits(memDc, iconInfo.hbmColor, 0, static_cast<UINT>(height), pixels.data(), &dibInfo, DIB_RGB_COLORS);

    bool hasAlpha = false;
    for (const uint32_t p : pixels)
    {
        if ((p >> 24) != 0)
        {
            hasAlpha = true;
            break;
        }
    }

    if (!hasAlpha)
    {
        // Classic icon with no per-pixel alpha: reconstruct opacity from
        // the AND mask (opaque where the mask is 0, transparent where 1) --
        // without this, non-alpha icons would render with a black background
        // in D2D instead of blending into the card.
        std::vector<uint32_t> maskPixels(pixels.size(), 0);
        GetDIBits(memDc, iconInfo.hbmMask, 0, static_cast<UINT>(height), maskPixels.data(), &dibInfo, DIB_RGB_COLORS);

        for (size_t i = 0; i < pixels.size(); ++i)
        {
            const bool transparent = (maskPixels[i] & 0x00FFFFFFu) != 0;
            pixels[i] = (pixels[i] & 0x00FFFFFFu) | (transparent ? 0x00000000u : 0xFF000000u);
        }
    }

    ReleaseDC(nullptr, screenDc);
    DeleteDC(memDc);

    if (scanLines == 0)
    {
        return nullptr;
    }

    PremultiplyInPlace(pixels);

    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bitmap;
    ctx->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
        pixels.data(), static_cast<UINT32>(width) * 4, props, &bitmap);

    return bitmap;
}

} // namespace winmix::app
