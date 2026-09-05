#include "MainWindow.h"
#include "Theme.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace winmix::app {

namespace {

constexpr wchar_t kClassName[] = L"WinMixCppWindowClass";
constexpr wchar_t kWindowTitle[] = L"WinMix (C++)";

constexpr float kMargin = 12.0f;
constexpr float kCardWidth = 64.0f;
constexpr float kCardMargin = 8.0f;
constexpr float kCardPaddingX = 6.0f;
constexpr float kCardPaddingTop = 10.0f;
constexpr float kHeaderHeight = 70.0f;
constexpr float kIconSize = 20.0f;
constexpr float kFaderHeight = 130.0f;
constexpr float kLabelHeight = 18.0f;
constexpr float kMuteHeight = 26.0f;
constexpr float kComboHeight = 22.0f;
constexpr float kMeterHeight = 3.0f;
constexpr float kNameHeight = 18.0f;
constexpr float kRowGap = 6.0f;

} // namespace

MainWindow::MainWindow(HINSTANCE hInstance)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainWindow::WndProcThunk;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, kWindowTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 480,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_)
    {
        throw std::runtime_error("CreateWindowExW failed");
    }

    resources_ = std::make_unique<render::DeviceResources>(hwnd_);
    CreateBrushes();
    CreateTextFormats();

    outputCombo_ = std::make_unique<controls::ComboBox>(hwnd_);
    outputCombo_->SetItems({L"Speakers (Realtek Audio)", L"Headphones (USB)"});
    outputCombo_->SetSelectedIndex(0);

    inputCombo_ = std::make_unique<controls::ComboBox>(hwnd_);
    inputCombo_->SetItems({L"Microphone Array", L"USB Microphone"});
    inputCombo_->SetSelectedIndex(0);

    masterFader_.SetValue(0.8);

    const std::vector<std::wstring> fakeNames = {
        L"Google Chrome", L"Spotify", L"Discord", L"Visual Studio",
        L"Windows Explorer", L"Steam", L"System sounds", L"VLC media player",
    };

    for (size_t i = 0; i < fakeNames.size(); ++i)
    {
        ChannelStrip strip;
        strip.data.name = fakeNames[i];
        strip.data.volume = 0.3 + 0.08 * static_cast<double>(i % 6);
        strip.data.muted = (i == 2);
        strip.data.peak = (i % 3 == 0) ? 0.35f : 0.0f;
        strip.data.active = (i % 3 != 1);

        strip.fader.SetValue(strip.data.volume);
        strip.mute.SetMuted(strip.data.muted);
        strip.meter.SetLevel(strip.data.peak);

        strip.outputCombo = std::make_unique<controls::ComboBox>(hwnd_);
        strip.outputCombo->SetItems({L"Default", L"Speakers (Realtek Audio)", L"Headphones (USB)"});
        strip.outputCombo->SetSelectedIndex(0);

        strips_.push_back(std::move(strip));
    }

    Layout();
}

MainWindow::~MainWindow()
{
    if (hwnd_)
    {
        DestroyWindow(hwnd_);
    }
}

void MainWindow::Show(int cmdShow)
{
    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
}

int MainWindow::RunMessageLoop()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    return self ? self->WndProc(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (resources_)
        {
            Render();
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (resources_ && wParam != SIZE_MINIMIZED)
        {
            resources_->Resize(LOWORD(lParam), HIWORD(lParam));
            Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        OnLButtonDown(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        ReleaseCapture();
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

    case WM_ERASEBKGND:
        // D2D repaints the whole surface every frame; skip GDI's clear to
        // avoid a flicker/flash between the two.
        return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::CreateBrushes()
{
    auto* ctx = resources_->Context();
    ctx->CreateSolidColorBrush(theme::kPanel, &panelBrush_);
    ctx->CreateSolidColorBrush(theme::kBorder, &borderBrush_);
    ctx->CreateSolidColorBrush(theme::kText, &textBrush_);
    ctx->CreateSolidColorBrush(theme::kSubtleText, &subtleTextBrush_);
    ctx->CreateSolidColorBrush(theme::kAccent, &accentBrush_);
    ctx->CreateSolidColorBrush(theme::kAccentHover, &accentHoverBrush_);
    ctx->CreateSolidColorBrush(theme::kTrack, &trackBrush_);
    ctx->CreateSolidColorBrush(theme::kMeter, &meterBrush_);
    ctx->CreateSolidColorBrush(theme::kMutedRed, &mutedRedBrush_);
    ctx->CreateSolidColorBrush(theme::kWindow, &windowRingBrush_);
}

void MainWindow::CreateTextFormats()
{
    auto* dwrite = resources_->DWriteFactory();

    dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &labelFormat_);
    labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &nameFormat_);
    nameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    nameFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    nameFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &comboFormat_);
    comboFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    comboFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    comboFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
}

void MainWindow::Layout()
{
    RECT client;
    GetClientRect(hwnd_, &client);
    const float clientHeight = static_cast<float>(client.bottom - client.top);

    outputCombo_->SetBounds(D2D1::RectF(kMargin + 56.0f, kMargin, kMargin + 240.0f, kMargin + 24.0f));
    inputCombo_->SetBounds(D2D1::RectF(kMargin + 56.0f, kMargin + 32.0f, kMargin + 240.0f, kMargin + 56.0f));

    const float stripsTop = kMargin + kHeaderHeight;
    const float stripsHeight = clientHeight - stripsTop - kMargin;

    float x = kMargin - scrollOffsetX_;

    masterLayout_ = LayoutStrip(x, stripsTop, stripsHeight, masterFader_, masterMute_, nullptr, nullptr);
    x += kCardWidth + kCardMargin;

    for (auto& strip : strips_)
    {
        strip.layout = LayoutStrip(x, stripsTop, stripsHeight, strip.fader, strip.mute,
                                    &strip.meter, strip.outputCombo.get());
        x += kCardWidth + kCardMargin;
    }

    contentWidth_ = (x - kCardMargin) - (kMargin - scrollOffsetX_);
}

StripLayout MainWindow::LayoutStrip(float left, float top, float height,
                                     controls::FaderControl& fader, controls::MuteToggle& mute,
                                     controls::PeakMeter* meter, controls::ComboBox* outputCombo)
{
    StripLayout layout;
    layout.card = D2D1::RectF(left, top, left + kCardWidth, top + height);

    const float cx0 = left + kCardPaddingX;
    const float cx1 = left + kCardWidth - kCardPaddingX;
    float y = top + kCardPaddingTop;

    const float iconCenterX = left + kCardWidth / 2.0f;
    layout.icon = D2D1::RectF(iconCenterX - kIconSize / 2.0f, y, iconCenterX + kIconSize / 2.0f, y + kIconSize);
    y += kIconSize + kRowGap;

    const float faderTop = y;
    const float faderBottom = faderTop + kFaderHeight;
    fader.SetBounds(D2D1::RectF(left + (kCardWidth - 24.0f) / 2.0f, faderTop,
                                 left + (kCardWidth + 24.0f) / 2.0f, faderBottom));
    y = faderBottom + kRowGap;

    layout.percentLabel = D2D1::RectF(cx0, y, cx1, y + kLabelHeight);
    y += kLabelHeight + kRowGap;

    mute.SetBounds(D2D1::RectF(left + (kCardWidth - 30.0f) / 2.0f, y,
                                left + (kCardWidth + 30.0f) / 2.0f, y + kMuteHeight));
    y += kMuteHeight + kRowGap;

    if (outputCombo)
    {
        outputCombo->SetBounds(D2D1::RectF(cx0, y, cx1, y + kComboHeight));
        y += kComboHeight + kRowGap;
    }

    if (meter)
    {
        meter->SetBounds(D2D1::RectF(cx0, y, cx1, y + kMeterHeight));
        y += kMeterHeight + kRowGap;
    }

    layout.nameLabel = D2D1::RectF(cx0, y, cx1, y + kNameHeight);

    return layout;
}

void MainWindow::Render()
{
    auto* ctx = resources_->Context();
    resources_->BeginDraw();

    ctx->Clear(theme::kWindow);

    ctx->DrawText(L"Output", 6, labelFormat_.Get(),
                  D2D1::RectF(kMargin, kMargin, kMargin + 50.0f, kMargin + 24.0f), subtleTextBrush_.Get());
    outputCombo_->Draw(ctx, comboFormat_.Get(), panelBrush_.Get(), borderBrush_.Get(), textBrush_.Get(), subtleTextBrush_.Get());

    ctx->DrawText(L"Input", 5, labelFormat_.Get(),
                  D2D1::RectF(kMargin, kMargin + 32.0f, kMargin + 50.0f, kMargin + 56.0f), subtleTextBrush_.Get());
    inputCombo_->Draw(ctx, comboFormat_.Get(), panelBrush_.Get(), borderBrush_.Get(), textBrush_.Get(), subtleTextBrush_.Get());

    RECT client;
    GetClientRect(hwnd_, &client);
    const D2D1_RECT_F clip = D2D1::RectF(
        0.0f, masterLayout_.card.top - 4.0f,
        static_cast<float>(client.right), static_cast<float>(client.bottom));
    ctx->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    DrawStrip(masterLayout_, masterFader_, masterMute_, nullptr, nullptr, L"Device", true);

    for (auto& strip : strips_)
    {
        DrawStrip(strip.layout, strip.fader, strip.mute, &strip.meter, strip.outputCombo.get(),
                  strip.data.name, strip.data.active);
    }

    ctx->PopAxisAlignedClip();

    if (!resources_->EndDraw())
    {
        CreateBrushes();
        CreateTextFormats();
    }
}

void MainWindow::DrawStrip(const StripLayout& layout, controls::FaderControl& fader, controls::MuteToggle& mute,
                            controls::PeakMeter* meter, controls::ComboBox* outputCombo,
                            const std::wstring& name, bool active)
{
    auto* ctx = resources_->Context();

    ctx->FillRoundedRectangle(D2D1::RoundedRect(layout.card, 6.0f, 6.0f), panelBrush_.Get());
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(layout.card, 6.0f, 6.0f), borderBrush_.Get(), 1.0f);

    // Idle (non-audio-producing) cards render at reduced opacity but stay
    // listed, rather than disappearing -- drawn via a layer so every element
    // (icon, fader, labels, meter) dims together in one pass.
    D2D1_LAYER_PARAMETERS1 layerParams = D2D1::LayerParameters1();
    layerParams.opacity = active ? 1.0f : 0.5f;
    ctx->PushLayer(layerParams, nullptr);

    const float iconCx = (layout.icon.left + layout.icon.right) / 2.0f;
    const float iconCy = (layout.icon.top + layout.icon.bottom) / 2.0f;
    ctx->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(iconCx, iconCy), (layout.icon.right - layout.icon.left) / 2.0f,
                      (layout.icon.bottom - layout.icon.top) / 2.0f),
        trackBrush_.Get());
    if (!name.empty())
    {
        const std::wstring initial(1, static_cast<wchar_t>(towupper(name[0])));
        ctx->DrawText(initial.c_str(), 1, labelFormat_.Get(), layout.icon, textBrush_.Get());
    }

    fader.Draw(ctx, trackBrush_.Get(), accentBrush_.Get(), accentBrush_.Get(), accentHoverBrush_.Get(), windowRingBrush_.Get());

    const int percent = static_cast<int>(std::lround(fader.Value() * 100.0));
    const std::wstring percentText = std::to_wstring(percent) + L"%";
    ctx->DrawText(percentText.c_str(), static_cast<UINT32>(percentText.size()), labelFormat_.Get(),
                  layout.percentLabel, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    mute.Draw(ctx, trackBrush_.Get(), textBrush_.Get(), mutedRedBrush_.Get());

    if (outputCombo)
    {
        outputCombo->Draw(ctx, comboFormat_.Get(), panelBrush_.Get(), borderBrush_.Get(), textBrush_.Get(), subtleTextBrush_.Get());
    }

    if (meter)
    {
        meter->Draw(ctx, trackBrush_.Get(), meterBrush_.Get());
    }

    ctx->DrawText(name.c_str(), static_cast<UINT32>(name.size()), nameFormat_.Get(),
                  layout.nameLabel, subtleTextBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    ctx->PopLayer();
}

void MainWindow::OnLButtonDown(POINT pt)
{
    const D2D1_POINT_2F p = D2D1::Point2F(static_cast<float>(pt.x), static_cast<float>(pt.y));

    if (outputCombo_->HitTest(p)) { outputCombo_->OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (inputCombo_->HitTest(p)) { inputCombo_->OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }

    if (masterMute_.HitTest(p)) { masterMute_.OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (masterFader_.OnLButtonDown(p)) { draggingFader_ = &masterFader_; InvalidateRect(hwnd_, nullptr, FALSE); return; }

    for (auto& strip : strips_)
    {
        if (strip.outputCombo->HitTest(p)) { strip.outputCombo->OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (strip.mute.HitTest(p)) { strip.mute.OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (strip.fader.OnLButtonDown(p)) { draggingFader_ = &strip.fader; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    }
}

void MainWindow::OnMouseMove(POINT pt)
{
    const D2D1_POINT_2F p = D2D1::Point2F(static_cast<float>(pt.x), static_cast<float>(pt.y));

    if (draggingFader_)
    {
        draggingFader_->OnMouseMove(p);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    masterFader_.OnMouseMove(p);
    masterMute_.OnMouseMove(p);
    for (auto& strip : strips_)
    {
        strip.fader.OnMouseMove(p);
        strip.mute.OnMouseMove(p);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnLButtonUp(POINT /*pt*/)
{
    if (draggingFader_)
    {
        draggingFader_->OnLButtonUp();
        draggingFader_ = nullptr;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseWheel(int delta)
{
    RECT client;
    GetClientRect(hwnd_, &client);
    const float viewWidth = static_cast<float>(client.right - client.left) - 2.0f * kMargin;

    const float maxScroll = std::max(0.0f, contentWidth_ - viewWidth);
    scrollOffsetX_ -= static_cast<float>(delta) / 120.0f * 60.0f;
    scrollOffsetX_ = std::clamp(scrollOffsetX_, 0.0f, maxScroll);

    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace winmix::app
