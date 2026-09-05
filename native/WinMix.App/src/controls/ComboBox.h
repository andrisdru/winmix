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

    void SetBounds(D2D1_RECT_F bounds) { bounds_ = bounds; }
    const D2D1_RECT_F& Bounds() const { return bounds_; }

    void SetItems(std::vector<std::wstring> items) { items_ = std::move(items); }
    void SetSelectedIndex(int index) { selectedIndex_ = index; } // programmatic, no onChange
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

    HWND owner_;
    HWND popupHwnd_ = nullptr;
    D2D1_RECT_F bounds_{};
    std::vector<std::wstring> items_;
    int selectedIndex_ = -1;
    int hoveredIndex_ = -1;

    HBRUSH panelGdiBrush_ = nullptr;
    HBRUSH hoverGdiBrush_ = nullptr;
    COLORREF textColorRef_ = RGB(255, 255, 255);

    mutable Microsoft::WRL::ComPtr<ID2D1PathGeometry> caretGeometry_;

    static constexpr int kRowHeight = 24;
};

} // namespace winmix::app::controls
