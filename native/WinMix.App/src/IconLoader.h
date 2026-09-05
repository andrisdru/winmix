#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <d2d1_1.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace winmix::app {

// Loads and caches per-process icons as D2D bitmaps, keyed by executable
// path (case-insensitive) -- mirrors the .NET port's IconLoader, including
// caching failures (a missing icon is cosmetic, not worth retrying every
// poll).
class IconLoader
{
public:
    // Returns a cached bitmap, or nullptr if none could be loaded.
    // isSystemSounds bypasses per-path lookup for the synthetic "System
    // sounds" row, using the OS "info" stock icon instead.
    ID2D1Bitmap* ForExecutable(ID2D1DeviceContext* ctx, const std::optional<std::wstring>& executablePath, bool isSystemSounds);

private:
    static Microsoft::WRL::ComPtr<ID2D1Bitmap> Load(ID2D1DeviceContext* ctx, const std::wstring& path);
    static Microsoft::WRL::ComPtr<ID2D1Bitmap> FromHIcon(ID2D1DeviceContext* ctx, HICON icon);

    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> cache_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> systemSoundsIcon_;
    bool systemSoundsLoaded_ = false;
};

} // namespace winmix::app
