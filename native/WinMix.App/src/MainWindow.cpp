#include "MainWindow.h"
#include "Theme.h"

#include "winmix/audio/VolumeCurve.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using Microsoft::WRL::ComPtr;
using winmix::audio::VolumeCurve;

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

// Fast enough that the peak meters read as continuous, slow enough that
// re-enumerating sessions stays free -- matches the .NET version's poll
// interval exactly.
constexpr UINT_PTR kPollTimerId = 1;
constexpr UINT kPollIntervalMs = 100;

// How far the device's reported scalar may drift from what the fader
// implies before it's treated as a genuine external change. WASAPI
// quantizes what it stores, so a strict comparison would never match and
// every poll would yank the fader out from under a live drag.
constexpr float kScalarEpsilon = 0.01f;

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
    outputCombo_->onChange = [this](int index)
    {
        if (index >= 0 && index < static_cast<int>(outputDeviceIds_.size()))
        {
            try
            {
                audioService_.SetDefaultOutputDevice(outputDeviceIds_[index]);
            }
            catch (const std::exception&)
            {
            }
        }
    };

    inputCombo_ = std::make_unique<controls::ComboBox>(hwnd_);
    inputCombo_->onChange = [this](int index)
    {
        if (index >= 0 && index < static_cast<int>(inputDeviceIds_.size()))
        {
            try
            {
                audioService_.SetDefaultInputDevice(inputDeviceIds_[index]);
            }
            catch (const std::exception&)
            {
            }
        }
    };
    // Real items are populated by the first Poll() below.

    // User-driven only -- SetValue() (used when a poll adopts the device's
    // own value) deliberately does not invoke onChange, so there is no risk
    // of a poll echoing straight back into a WASAPI write.
    masterFader_.onChange = [this](double position)
    {
        try
        {
            audioService_.SetMasterVolume(VolumeCurve::ToScalar(position));
        }
        catch (const std::exception&)
        {
            // The default endpoint vanished mid-gesture; the next poll
            // reflects reality.
        }
    };
    masterMute_.onChange = [this](bool muted)
    {
        try
        {
            audioService_.SetMasterMuted(muted);
        }
        catch (const std::exception&)
        {
        }
    };

    Poll();
    Layout();

    SetTimer(hwnd_, kPollTimerId, kPollIntervalMs, nullptr);
}

MainWindow::~MainWindow()
{
    if (hwnd_)
    {
        KillTimer(hwnd_, kPollTimerId);
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

    case WM_TIMER:
        if (wParam == kPollTimerId)
        {
            Poll();
            Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
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
                  strip.name, strip.active);
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

void MainWindow::Poll()
{
    try
    {
        const auto snapshots = audioService_.Refresh();
        SyncMaster();
        SyncOutputDevices();
        SyncInputDevices();
        ReconcileSessions(snapshots);
    }
    catch (const std::exception&)
    {
        // Typically no active render endpoint at all; leave the last known
        // state on screen rather than tearing down the poll loop over it.
    }
}

void MainWindow::SyncMaster()
{
    const float deviceScalar = audioService_.GetMasterVolume();
    const float currentScalar = VolumeCurve::ToScalar(masterFader_.Value());
    if (std::abs(currentScalar - deviceScalar) > kScalarEpsilon)
    {
        masterFader_.SetValue(VolumeCurve::ToPosition(deviceScalar));
    }

    masterMute_.SetMuted(audioService_.GetMasterMuted());
}

void MainWindow::SyncOutputDevices()
{
    // Skip while open: replacing the item list out from under an
    // in-progress click would be jarring, and it costs nothing to pick it
    // up on the next tick once the user closes it.
    if (outputCombo_->IsOpen())
    {
        return;
    }

    const auto devices = audioService_.ListOutputDevices();
    std::vector<std::wstring> items;
    items.reserve(devices.size());
    outputDeviceIds_.clear();
    outputDeviceIds_.reserve(devices.size());
    int defaultIndex = 0;
    for (size_t i = 0; i < devices.size(); ++i)
    {
        items.push_back(devices[i].friendlyName);
        outputDeviceIds_.push_back(devices[i].id);
        if (devices[i].isDefault)
        {
            defaultIndex = static_cast<int>(i);
        }
    }

    outputCombo_->SetItems(std::move(items));
    outputCombo_->SetSelectedIndex(defaultIndex);
}

void MainWindow::SyncInputDevices()
{
    if (inputCombo_->IsOpen())
    {
        return;
    }

    const auto devices = audioService_.ListInputDevices();
    std::vector<std::wstring> items;
    items.reserve(devices.size());
    inputDeviceIds_.clear();
    inputDeviceIds_.reserve(devices.size());
    int defaultIndex = 0;
    for (size_t i = 0; i < devices.size(); ++i)
    {
        items.push_back(devices[i].friendlyName);
        inputDeviceIds_.push_back(devices[i].id);
        if (devices[i].isDefault)
        {
            defaultIndex = static_cast<int>(i);
        }
    }

    inputCombo_->SetItems(std::move(items));
    inputCombo_->SetSelectedIndex(defaultIndex);
}

void MainWindow::ReconcileSessions(const std::vector<winmix::audio::AudioSessionSnapshot>& snapshots)
{
    std::unordered_set<std::wstring> incomingIds;
    incomingIds.reserve(snapshots.size());
    for (const auto& s : snapshots)
    {
        incomingIds.insert(s.instanceId);
    }

    for (auto it = strips_.begin(); it != strips_.end();)
    {
        if (!incomingIds.contains(it->instanceId))
        {
            it = strips_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Reserved so every pointer taken into `present` below survives every
    // push_back in this call. New rows go on the end (arrival order), never
    // resorted -- re-sorting live would make cards jump around every time an
    // app starts or stops producing sound.
    strips_.reserve(snapshots.size());

    std::unordered_map<std::wstring, ChannelStrip*> present;
    present.reserve(strips_.size());
    for (auto& strip : strips_)
    {
        present[strip.instanceId] = &strip;
    }

    for (const auto& snapshot : snapshots)
    {
        auto it = present.find(snapshot.instanceId);
        if (it != present.end())
        {
            SyncStrip(*it->second, snapshot);
        }
        else
        {
            strips_.push_back(CreateStrip(snapshot));
        }
    }
}

void MainWindow::SyncStrip(ChannelStrip& strip, const winmix::audio::AudioSessionSnapshot& snapshot)
{
    strip.name = snapshot.displayName;
    strip.active = snapshot.IsActive();
    strip.meter.SetLevel(snapshot.peakLevel);
    strip.mute.SetMuted(snapshot.isMuted);

    const float currentScalar = VolumeCurve::ToScalar(strip.fader.Value());
    if (std::abs(currentScalar - snapshot.volume) > kScalarEpsilon)
    {
        strip.fader.SetValue(VolumeCurve::ToPosition(snapshot.volume));
    }
}

ChannelStrip MainWindow::CreateStrip(const winmix::audio::AudioSessionSnapshot& snapshot)
{
    ChannelStrip strip;
    strip.instanceId = snapshot.instanceId;
    strip.pid = snapshot.pid;
    strip.name = snapshot.displayName;
    strip.active = snapshot.IsActive();
    strip.fader.SetValue(VolumeCurve::ToPosition(snapshot.volume));
    strip.mute.SetMuted(snapshot.isMuted);
    strip.meter.SetLevel(snapshot.peakLevel);

    // "Default" (index 0, no pin) followed by every active render device.
    // Populated once at creation, not refreshed every poll -- a device
    // plugged in after this session started won't appear until the row is
    // recreated, a corner case not worth the extra per-strip bookkeeping.
    std::vector<std::wstring> items{L"Default"};
    std::vector<std::wstring> deviceIds;
    for (const auto& device : audioService_.ListOutputDevices())
    {
        items.push_back(device.friendlyName);
        deviceIds.push_back(device.id);
    }

    strip.outputCombo = std::make_unique<controls::ComboBox>(hwnd_);
    strip.outputCombo->SetItems(std::move(items));
    strip.outputCombo->SetSelectedIndex(0);
    strip.outputCombo->onChange = [this, pid = strip.pid, deviceIds](int index)
    {
        // index 0 is "Default" -> clear the pin (nullopt); index i>=1 maps
        // to deviceIds[i - 1].
        const std::optional<std::wstring> deviceId =
            (index >= 1 && index - 1 < static_cast<int>(deviceIds.size()))
                ? std::optional<std::wstring>(deviceIds[index - 1])
                : std::nullopt;
        audioService_.SetAppOutputDevice(pid, deviceId);
    };

    const std::wstring instanceId = strip.instanceId;
    strip.fader.onChange = [this, instanceId](double position)
    {
        audioService_.SetVolume(instanceId, VolumeCurve::ToScalar(position));
    };
    strip.mute.onChange = [this, instanceId](bool muted)
    {
        audioService_.SetMute(instanceId, muted);
    };

    return strip;
}

} // namespace winmix::app
