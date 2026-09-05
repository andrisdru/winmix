#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

namespace winmix::app::controls {

// A themed dropdown: a Direct2D-drawn closed "button" plus a separate,
// GDI-rendered popup window for the open list. The popup deliberately uses
// plain GDI rather than its own DXGI swap chain -- it is a small,
// infrequently-shown overlay, and duplicating the whole D3D/D2D device
// setup for it would not be worth the ceremony. This is the single most
// involved control in the port: Win32 has no themed built-in this custom.
class ComboBox
{
public:
    explicit ComboBox(HWND owner);
    ~ComboBox();

    ComboBox(const ComboBox&) = delete;
    ComboBox& operator=(const ComboBox&) = delete;

    void SetBounds(D2D1_RECT_F bounds);
    const D2D1_RECT_F& Bounds() const { return bounds_; }

    // DPI scale factor (GetDpiForWindow()/96). Drives both the D2D-drawn
    // closed button's fixed-DIP details (corner radius, caret) and the
    // GDI popup's font size/row height, since bounds_ alone (already scaled
    // by the caller) says nothing about how large text inside it should be.
    void SetScale(float scale);

    void SetItems(std::vector<std::wstring> items, std::vector<std::wstring> compactLabels = {});
    void SetSelectedIndex(int index); // programmatic, no onChange
    int SelectedIndex() const { return selectedIndex_; }
    std::wstring SelectedText() const;

    bool IsOpen() const { return popupHwnd_ != nullptr; }
    void CloseIfOpen();

    void Draw(ID2D1DeviceContext* ctx, IDWriteTextFormat* textFormat,
              ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* borderBrush,
              ID2D1SolidColorBrush* textBrush, ID2D1SolidColorBrush* caretBrush) const;

    bool HitTest(D2D1_POINT_2F pt) const;
    void OnClick(D2D1_POINT_2F pt);

    std::function<void(int)> onChange;

private:
    static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OpenPopup();
    void PaintPopup(HWND hwnd);
    int ItemAtY(int y) const;
    void EnsureGdiFont();
    void UpdateTooltip();
    int RowHeight() const;

    HWND owner_;
    HWND popupHwnd_ = nullptr;
    HWND tooltipHwnd_ = nullptr;
    std::wstring tooltipText_;
    D2D1_RECT_F bounds_{};
    float scale_ = 1.0f;
    std::vector<std::wstring> items_;
    std::vector<std::wstring> compactLabels_;
    int selectedIndex_ = -1;
    int hoveredIndex_ = -1;

    HBRUSH panelGdiBrush_ = nullptr;
    HBRUSH hoverGdiBrush_ = nullptr;
    COLORREF textColorRef_ = RGB(255, 255, 255);
    HFONT gdiFont_ = nullptr;
    float gdiFontScale_ = 0.0f; // scale_ the current gdiFont_ was built for

    mutable Microsoft::WRL::ComPtr<ID2D1PathGeometry> caretGeometry_;

    static constexpr int kBaseRowHeight = 24;
};

} // namespace winmix::app::controls
