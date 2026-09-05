#include "controls/ComboBox.h"
#include "Theme.h"

#include <windowsx.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace winmix::app::controls {

namespace {
constexpr wchar_t kPopupClassName[] = L"WinMixCppComboPopup";
} // namespace

ComboBox::ComboBox(HWND owner) : owner_(owner)
{
    panelGdiBrush_ = CreateSolidBrush(theme::ToColorRef(theme::kPanel));
    hoverGdiBrush_ = CreateSolidBrush(theme::ToColorRef(theme::kTrack));
    textColorRef_ = theme::ToColorRef(theme::kText);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ComboBox::PopupWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kPopupClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Harmless if another ComboBox instance already registered this class.
    RegisterClassExW(&wc);
}

ComboBox::~ComboBox()
{
    CloseIfOpen();
    DeleteObject(panelGdiBrush_);
    DeleteObject(hoverGdiBrush_);
}

std::wstring ComboBox::SelectedText() const
{
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(items_.size()))
    {
        return L"";
    }
    return items_[selectedIndex_];
}

bool ComboBox::HitTest(D2D1_POINT_2F pt) const
{
    return pt.x >= bounds_.left && pt.x <= bounds_.right && pt.y >= bounds_.top && pt.y <= bounds_.bottom;
}

void ComboBox::OnClick(D2D1_POINT_2F pt)
{
    if (!HitTest(pt))
    {
        return;
    }

    if (IsOpen())
    {
        CloseIfOpen();
    }
    else
    {
        OpenPopup();
    }
}

void ComboBox::CloseIfOpen()
{
    if (popupHwnd_)
    {
        DestroyWindow(popupHwnd_);
        popupHwnd_ = nullptr;
    }
}

void ComboBox::OpenPopup()
{
    if (popupHwnd_ != nullptr || items_.empty())
    {
        return;
    }

    POINT topLeft{static_cast<int>(bounds_.left), static_cast<int>(bounds_.bottom) + 2};
    ClientToScreen(owner_, &topLeft);

    const int width = std::max(120, static_cast<int>(bounds_.right - bounds_.left));
    const int height = static_cast<int>(items_.size()) * kRowHeight + 4;

    popupHwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kPopupClassName, L"",
        WS_POPUP | WS_BORDER,
        topLeft.x, topLeft.y, width, height,
        owner_, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!popupHwnd_)
    {
        return;
    }

    SetWindowLongPtrW(popupHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(popupHwnd_, SW_SHOW);
}

int ComboBox::ItemAtY(int y) const
{
    if (y < 2)
    {
        return -1;
    }
    const int index = (y - 2) / kRowHeight;
    return (index >= 0 && index < static_cast<int>(items_.size())) ? index : -1;
}

void ComboBox::PaintPopup(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(hdc, &client, panelGdiBrush_);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColorRef_);

    for (size_t i = 0; i < items_.size(); ++i)
    {
        RECT row{2, 2 + static_cast<int>(i) * kRowHeight, client.right - 2, 2 + static_cast<int>(i + 1) * kRowHeight};
        if (static_cast<int>(i) == hoveredIndex_)
        {
            FillRect(hdc, &row, hoverGdiBrush_);
        }
        RECT textRect = row;
        textRect.left += 8;
        DrawTextW(hdc, items_[i].c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ComboBox::PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<ComboBox*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_PAINT:
        if (self)
        {
            self->PaintPopup(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (self)
        {
            const int hovered = self->ItemAtY(GET_Y_LPARAM(lParam));
            if (hovered != self->hoveredIndex_)
            {
                self->hoveredIndex_ = hovered;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (self)
        {
            const int clicked = self->ItemAtY(GET_Y_LPARAM(lParam));
            if (clicked >= 0)
            {
                self->selectedIndex_ = clicked;
                if (self->onChange)
                {
                    self->onChange(clicked);
                }
            }
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        if (self)
        {
            self->popupHwnd_ = nullptr;
        }
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ComboBox::Draw(ID2D1DeviceContext* ctx, IDWriteTextFormat* textFormat,
                     ID2D1SolidColorBrush* panelBrush, ID2D1SolidColorBrush* borderBrush,
                     ID2D1SolidColorBrush* textBrush, ID2D1SolidColorBrush* caretBrush) const
{
    ctx->FillRoundedRectangle(D2D1::RoundedRect(bounds_, 4.0f, 4.0f), panelBrush);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(bounds_, 4.0f, 4.0f), borderBrush, 1.0f);

    D2D1_RECT_F textRect = bounds_;
    textRect.left += 8.0f;
    textRect.right -= 20.0f;

    const std::wstring text = SelectedText();
    ctx->DrawText(text.c_str(), static_cast<UINT32>(text.size()), textFormat, textRect, textBrush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP);

    ComPtr<ID2D1Factory> factory;
    ctx->GetFactory(&factory);

    if (!caretGeometry_)
    {
        factory->CreatePathGeometry(&caretGeometry_);
        ComPtr<ID2D1GeometrySink> sink;
        caretGeometry_->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(-4.0f, -2.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(4.0f, -2.0f));
        sink->AddLine(D2D1::Point2F(0.0f, 3.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
    }

    const float cx = bounds_.right - 12.0f;
    const float cy = (bounds_.top + bounds_.bottom) / 2.0f;

    ComPtr<ID2D1TransformedGeometry> transformed;
    factory->CreateTransformedGeometry(caretGeometry_.Get(), D2D1::Matrix3x2F::Translation(cx, cy), &transformed);
    ctx->FillGeometry(transformed.Get(), caretBrush);
}

} // namespace winmix::app::controls
