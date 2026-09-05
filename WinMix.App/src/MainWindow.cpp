#include "MainWindow.h"
#include "Autostart.h"
#include "DeviceLabels.h"
#include "Theme.h"
#include "Version.h"
#include "resource.h"

#include "winmix/audio/VolumeCurve.h"

#include <windowsx.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using Microsoft::WRL::ComPtr;
using winmix::audio::VolumeCurve;

namespace winmix::app {

namespace {

constexpr wchar_t kClassName[] = L"WinMixWindowClass";
constexpr wchar_t kWindowTitle[] = L"WinMix";

constexpr float kMargin = 12.0f;
constexpr float kCardWidth = 84.0f;
constexpr float kCardMargin = 8.0f;
constexpr float kCardPaddingX = 6.0f;
constexpr float kCardPaddingTop = 10.0f;
constexpr float kCardPaddingBottom = 10.0f;
constexpr float kHeaderHeight = 70.0f;
constexpr float kIconSize = 20.0f;
// Floor only -- the fader itself stretches to fill whatever vertical room
// is left in the card (see LayoutStrip), matching the .NET version's
// Grid row Height="*" for the slider. Without a floor, a badly squashed
// window could invert the fader's top/bottom.
constexpr float kMinFaderHeight = 60.0f;
constexpr float kLabelHeight = 18.0f;
constexpr float kMuteHeight = 26.0f;
constexpr float kComboHeight = 32.0f;
constexpr float kDeviceLabelHeight = 12.0f;
constexpr float kDeviceLabelGap = 2.0f;
constexpr float kMeterHeight = 3.0f;
// Two lines: long app names (e.g. "Firefox Nightly Preview") wrap instead
// of ellipsis-truncating into an unreadable fragment -- see nameFormat_'s
// WRAP setting in CreateTextFormats.
constexpr float kNameHeight = 30.0f;
constexpr float kRowGap = 6.0f;

// Vertical space LayoutStrip needs for one full per-app card (the tallest
// strip variant -- it's the only one with both the output combo and the
// peak meter) at scale 1, assuming the fader is squashed to its floor.
// kMinHeight below is derived from this rather than a separate magic
// number, so the two can never drift apart and silently clip the last row.
constexpr float kStripContentHeight =
    kCardPaddingTop + kIconSize + kRowGap +
    kMinFaderHeight + kRowGap +
    kLabelHeight + kRowGap +
    kMuteHeight + kRowGap +
    2.0f * (kDeviceLabelHeight + kDeviceLabelGap + kComboHeight + kRowGap) +
    kMeterHeight + kRowGap +
    kNameHeight + kCardPaddingBottom;

// Slow enough that re-enumerating sessions stays free -- matches the .NET
// version's poll interval exactly. Peak meter smoothness no longer rides on
// this: see kMeterTimerId below.
constexpr UINT_PTR kPollTimerId = 1;
constexpr UINT kPollIntervalMs = 100;

// Drives PeakMeter::Advance() independently of the audio poll above, so the
// bars ease between polled values instead of stepping once per 100ms. Pure
// UI animation -- touches no COM/audio state -- so running it faster than
// the poll is free.
constexpr UINT_PTR kMeterTimerId = 2;
constexpr UINT kMeterIntervalMs = 16;
constexpr float kMeterFrameSeconds = kMeterIntervalMs / 1000.0f;

// How far the device's reported scalar may drift from what the fader
// implies before it's treated as a genuine external change. WASAPI
// quantizes what it stores, so a strict comparison would never match and
// every poll would yank the fader out from under a live drag.
constexpr float kScalarEpsilon = 0.01f;

// Content-driven width, capped in WM_GETMINMAXINFO; height stays freely
// resizable (see WM_NCHITTEST below), but never below what one full strip
// needs, or the bottom row (the name label) clips against the window edge.
constexpr LONG kMinWidth = 300;
constexpr LONG kMinHeight = static_cast<LONG>(kHeaderHeight + kStripContentHeight + 2.0f * kMargin);
constexpr LONG kMaxWidthMargin = 60;

// Height stays a fixed, comfortable size (matching the .NET version's
// Height="420") rather than tracking content -- the fader absorbs whatever
// room that leaves via its stretch in LayoutStrip, so there's never a dead
// gap at the bottom regardless of this value or the user's own resize.
constexpr float kDefaultHeight = 480.0f;

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kRouteWarningMessage = WM_APP + 2;

// Without this, a format with no-wrap + DRAW_TEXT_OPTIONS_CLIP just hard-cuts
// whatever doesn't fit at the layout rect's edge -- for LEADING alignment
// that reads as a word chopped off mid-letter ("Defa" for "Default"), and for
// CENTER alignment (the per-strip name label) it clips *both* ends at once,
// showing a garbled slice from the middle of the string. This appends "..."
// via DirectWrite's own trimming instead, matching the .NET version's
// TextTrimming=CharacterEllipsis.
void ApplyEllipsisTrimming(IDWriteFactory* dwrite, IDWriteTextFormat* format)
{
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    dwrite->CreateEllipsisTrimmingSign(format, &ellipsis);
    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    format->SetTrimming(&trimming, ellipsis.Get());
}

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

    // CreateWindowExW's width/height are literal physical pixels -- unlike
    // WPF, where a Height="420" window is DIP units the framework scales to
    // the monitor automatically, Win32 applies no such scaling. dpiScale_
    // itself needs an hwnd to compute (GetDpiForWindow), so this uses the
    // system DPI as the best available guess before one exists; WM_DPICHANGED
    // corrects it if the window ends up on a different-DPI monitor than
    // assumed here. Width starts at kMinWidth as a placeholder only --
    // Show()/ShowMixer() run the first Poll()+Layout() (which resizes the
    // window to fit the real strip count, matching the .NET version's
    // SizeToContent="Width") before the window is ever made visible.
    const float initialScale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    const int initialWidth = static_cast<int>(std::lround(static_cast<float>(kMinWidth) * initialScale));
    const int initialHeight = static_cast<int>(std::lround(kDefaultHeight * initialScale));

    const std::wstring windowTitle = std::wstring(kWindowTitle) + L" v" + kWinMixVersion;
    hwnd_ = CreateWindowExW(
        0, kClassName, windowTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, initialWidth, initialHeight,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_)
    {
        throw std::runtime_error("CreateWindowExW failed");
    }

    UpdateDpiScale();

    const HICON appIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));

    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    resources_ = std::make_unique<render::DeviceResources>(hwnd_);
    CreateBrushes();
    CreateTextFormats();

    tray_ = std::make_unique<TrayIcon>(hwnd_, kTrayCallbackMessage, appIcon);
    tray_->onOpen = [this]() { ShowMixer(); };
    tray_->onExit = [this]() { PostQuitMessage(0); };
    tray_->listInputDevices = [this]() { return ListInputDevicesForTray(); };
    tray_->setDefaultInputDevice = [this](const std::wstring& id)
    {
        try
        {
            audioService_.SetDefaultInputDevice(id);
        }
        catch (const std::exception&)
        {
        }
    };
    tray_->isAutostartEnabled = []() { return Autostart::IsEnabled(); };
    tray_->setAutostartEnabled = [](bool enabled) { Autostart::SetEnabled(enabled); };

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

    Layout();
}

MainWindow::~MainWindow()
{
    if (hwnd_)
    {
        StopPolling();
        tray_.reset(); // remove the tray icon before the window (and its HICON) go away
        DestroyWindow(hwnd_);
    }
}

void MainWindow::Show(int cmdShow)
{
    // Populate real sessions and let Layout() size the window to them
    // (SyncWindowWidthToContent) before the window is ever visible, rather
    // than showing it at the placeholder width and then visibly popping to
    // the real one a moment later.
    StartPolling();
    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
}

void MainWindow::ShowMixer()
{
    scrollOffsetX_ = 0.0f; // land on the master strip, matching the .NET port's ShowMixer
    StartPolling();
    Layout();
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::StartPolling()
{
    if (timerRunning_)
    {
        return;
    }
    timerRunning_ = true;
    SetTimer(hwnd_, kPollTimerId, kPollIntervalMs, nullptr);
    SetTimer(hwnd_, kMeterTimerId, kMeterIntervalMs, nullptr);
    Poll();
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::StopPolling()
{
    if (!timerRunning_)
    {
        return;
    }
    timerRunning_ = false;
    KillTimer(hwnd_, kPollTimerId);
    KillTimer(hwnd_, kMeterTimerId);
}

std::vector<TrayInputDevice> MainWindow::ListInputDevicesForTray()
{
    std::vector<TrayInputDevice> result;
    try
    {
        for (const auto& d : audioService_.ListInputDevices())
        {
            result.push_back(TrayInputDevice{d.id, d.friendlyName, d.isDefault});
        }
    }
    catch (const std::exception&)
    {
    }
    return result;
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
            const auto oldCount = strips_.size();
            const auto oldNextRowId = nextRowId_;
            Poll();
            if (strips_.size() != oldCount || nextRowId_ != oldNextRowId) Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (wParam == kMeterTimerId)
        {
            AnimateMeters();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case kRouteWarningMessage:
    {
        const bool input = lParam != 0;
        const auto it = std::find_if(strips_.begin(), strips_.end(),
            [&](const ChannelStrip& strip) { return strip.rowId == static_cast<uint64_t>(wParam); });
        if (it != strips_.end() && (input ? it->inputRouteWarningPending : it->routeWarningPending))
        {
            (input ? it->inputRouteWarningPending : it->routeWarningPending) = false;
            const std::wstring message = it->name + (input ?
                L" is still using another microphone. The Windows preference was saved.\n\n"
                L"Set the microphone inside the app to System Default so WinMix can control it. "
                L"If it already uses System Default, restart recording or the app." :
                L" is still playing through another output. The Windows preference was saved.\n\n"
                L"Set the audio output inside the app to System Default so WinMix can control it. "
                L"If it already uses System Default, restart playback or the app.");
            MessageBoxW(hwnd_, message.c_str(), input ? L"App has not switched input" : L"App has not switched output", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        // D2D repaints the whole surface every frame; skip GDI's clear to
        // avoid a flicker/flash between the two.
        return 1;

    case WM_NCHITTEST:
    {
        // Blocks manual width resizing (dragging left/right edges or
        // corners) while leaving height freely resizable via the top/bottom
        // edges -- matches the .NET port's WindowResize.cs exactly, just
        // without needing to hand-redeclare the HT* constants (they come
        // straight from <winuser.h> here).
        const LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
        switch (result)
        {
        case HTLEFT:
        case HTRIGHT:
        case HTTOPLEFT:
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
            return HTBORDER;
        default:
            return result;
        }
    }

    case WM_DPICHANGED:
    {
        // The window moved to a monitor with a different scale factor.
        // Update our own scale before the resulting resize/repaint (below)
        // uses it, then follow the OS's suggested rect for the new DPI.
        UpdateDpiScale();
        if (resources_)
        {
            CreateTextFormats();
        }

        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const LONG minWidth = static_cast<LONG>(kMinWidth * dpiScale_);
        const LONG minHeight = static_cast<LONG>(kMinHeight * dpiScale_);
        const LONG maxWidthMargin = static_cast<LONG>(kMaxWidthMargin * dpiScale_);
        // The window's *current* monitor, not the primary -- a long session
        // list should be able to use the full width of whichever screen the
        // window actually lives on.
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(monitor, &mi))
        {
            const LONG maxWidth = std::max(minWidth, (mi.rcWork.right - mi.rcWork.left) - maxWidthMargin);
            mmi->ptMaxTrackSize.x = maxWidth;
        }
        mmi->ptMinTrackSize.x = minWidth;
        RECT minimumClient{0, 0, minWidth, minHeight};
        AdjustWindowRectExForDpi(&minimumClient, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(hwnd));
        mmi->ptMinTrackSize.y = minimumClient.bottom - minimumClient.top;
        return 0;
    }

    case WM_CLOSE:
        // The X button / Alt+F4 never exits the process -- only the tray
        // menu's Exit does. Stop polling while hidden: there is nothing to
        // animate, so the COM traffic would be pure waste.
        StopPolling();
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        if (msg == kTrayCallbackMessage)
        {
            if (tray_)
            {
                tray_->OnCallback(lParam);
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::UpdateDpiScale()
{
    dpiScale_ = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
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
    // Reset first: ComPtr's operator& does not release an existing pointee,
    // and this can run again on a DPI change or a device-lost recreate.
    labelFormat_.Reset();
    nameFormat_.Reset();
    comboFormat_.Reset();
    stripComboFormat_.Reset();

    auto* dwrite = resources_->DWriteFactory();

    // D2D itself renders at a flat 96 DPI (DeviceResources) so hit-testing
    // stays aligned with rendering at every scale factor; font sizes have
    // to make up the difference themselves here, the way WPF's automatic
    // DIP scaling did for the .NET version, or text reads too small on any
    // display above 100%.
    const float labelSize = 11.0f * dpiScale_;
    const float nameSize = 10.0f * dpiScale_;
    const float comboSize = 11.0f * dpiScale_;

    dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, labelSize, L"en-us", &labelFormat_);
    labelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    labelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, nameSize, L"en-us", &nameFormat_);
    nameFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    nameFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    // Wraps onto a second line rather than the single-line format everything
    // else here uses -- kNameHeight gives it room for exactly two lines, and
    // the ellipsis trimming below still catches whatever doesn't fit even
    // then (a name with no natural break point, or three-line-plus names).
    nameFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    ApplyEllipsisTrimming(dwrite, nameFormat_.Get());

    dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, comboSize, L"en-us", &comboFormat_);
    comboFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    comboFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    comboFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ApplyEllipsisTrimming(dwrite, comboFormat_.Get());

    dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.0f * dpiScale_, L"en-us", &stripComboFormat_);
    stripComboFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    stripComboFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    stripComboFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    stripComboFormat_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 13.0f * dpiScale_, 10.0f * dpiScale_);
    ApplyEllipsisTrimming(dwrite, stripComboFormat_.Get());
}

void MainWindow::Layout()
{
    const float s = dpiScale_;

    RECT client;
    GetClientRect(hwnd_, &client);
    const float clientHeight = static_cast<float>(client.bottom - client.top);

    const float margin = kMargin * s;

    outputCombo_->SetScale(s);
    outputCombo_->SetBounds(D2D1::RectF(margin + 56.0f * s, margin, margin + 240.0f * s, margin + 24.0f * s));
    inputCombo_->SetScale(s);
    inputCombo_->SetBounds(D2D1::RectF(margin + 56.0f * s, margin + 32.0f * s, margin + 240.0f * s, margin + 56.0f * s));

    const float stripsTop = margin + kHeaderHeight * s;
    const float stripsHeight = clientHeight - stripsTop - margin;

    float x = margin - scrollOffsetX_;

    masterFader_.SetScale(s);
    masterMute_.SetScale(s);
    masterLayout_ = LayoutStrip(x, stripsTop, stripsHeight, masterFader_, masterMute_, nullptr, nullptr, nullptr);
    x += (kCardWidth + kCardMargin) * s;

    for (auto& strip : strips_)
    {
        strip.fader.SetScale(s);
        strip.mute.SetScale(s);
        if (strip.outputCombo)
        {
            strip.outputCombo->SetScale(s);
            strip.inputCombo->SetScale(s);
        }
        strip.layout = LayoutStrip(x, stripsTop, stripsHeight, strip.fader, strip.mute,
                                    &strip.meter, strip.outputCombo.get(), strip.inputCombo.get());
        x += (kCardWidth + kCardMargin) * s;
    }

    contentWidth_ = (x - kCardMargin * s) - (margin - scrollOffsetX_);

    SyncWindowWidthToContent();
}

void MainWindow::SyncWindowWidthToContent()
{
    // Mirrors the .NET version's SizeToContent="Width": the window always
    // matches the current strip count exactly, rather than sitting at some
    // arbitrary fixed width with empty space to the right of just a few
    // cards. Manual width dragging is already impossible (see the
    // WM_NCHITTEST remap below), so there's no user gesture this could fight.
    if (syncingWindowWidth_ || IsIconic(hwnd_) || IsZoomed(hwnd_))
    {
        return;
    }

    const float desiredClientWidthF = contentWidth_ + 2.0f * kMargin * dpiScale_;

    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    LONG maxWidth = std::numeric_limits<LONG>::max();
    if (GetMonitorInfoW(monitor, &mi))
    {
        const LONG minWidth = static_cast<LONG>(kMinWidth * dpiScale_);
        const LONG maxWidthMargin = static_cast<LONG>(kMaxWidthMargin * dpiScale_);
        maxWidth = std::max(minWidth, (mi.rcWork.right - mi.rcWork.left) - maxWidthMargin);
    }

    const LONG desiredClientWidth = std::clamp(
        static_cast<LONG>(std::lround(desiredClientWidthF)),
        static_cast<LONG>(kMinWidth * dpiScale_), maxWidth);

    RECT windowRect;
    RECT clientRect;
    GetWindowRect(hwnd_, &windowRect);
    GetClientRect(hwnd_, &clientRect);
    const LONG currentClientWidth = clientRect.right - clientRect.left;
    if (currentClientWidth == desiredClientWidth)
    {
        return;
    }

    const LONG nonClientWidth = (windowRect.right - windowRect.left) - currentClientWidth;
    const LONG windowHeight = windowRect.bottom - windowRect.top;

    syncingWindowWidth_ = true;
    SetWindowPos(hwnd_, nullptr, 0, 0, desiredClientWidth + nonClientWidth, windowHeight,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    syncingWindowWidth_ = false;
}

StripLayout MainWindow::LayoutStrip(float left, float top, float height,
                                     controls::FaderControl& fader, controls::MuteToggle& mute,
                                     controls::PeakMeter* meter, controls::ComboBox* outputCombo, controls::ComboBox* inputCombo)
{
    const float s = dpiScale_;
    const float cardWidth = kCardWidth * s;

    StripLayout layout;
    layout.card = D2D1::RectF(left, top, left + cardWidth, top + height);

    const float cx0 = left + kCardPaddingX * s;
    const float cx1 = left + cardWidth - kCardPaddingX * s;
    float y = top + kCardPaddingTop * s;

    const float iconSize = kIconSize * s;
    const float iconCenterX = left + cardWidth / 2.0f;
    layout.icon = D2D1::RectF(iconCenterX - iconSize / 2.0f, y, iconCenterX + iconSize / 2.0f, y + iconSize);
    y += iconSize + kRowGap * s;

    // The fader stretches to fill whatever vertical room is left in the
    // card, matching the .NET version's Grid row Height="*" for the slider
    // -- everything else here is a fixed size, so this is the one row that
    // makes the card fill the strip's full height with no dead space at the
    // bottom, at any window height the user resizes to.
    const float belowFaderHeight =
        kRowGap + kLabelHeight + kRowGap + kMuteHeight + kRowGap +
        (outputCombo ? kDeviceLabelHeight + kDeviceLabelGap + kComboHeight + kRowGap : 0.0f) +
        (inputCombo ? kDeviceLabelHeight + kDeviceLabelGap + kComboHeight + kRowGap : 0.0f) +
        (meter ? kMeterHeight + kRowGap : 0.0f) +
        kNameHeight + kCardPaddingBottom;
    const float faderHeight = std::max(kMinFaderHeight * s, height - (y - top) - belowFaderHeight * s);

    const float faderWidth = 24.0f * s;
    const float faderTop = y;
    const float faderBottom = faderTop + faderHeight;
    fader.SetBounds(D2D1::RectF(left + (cardWidth - faderWidth) / 2.0f, faderTop,
                                 left + (cardWidth + faderWidth) / 2.0f, faderBottom));
    y = faderBottom + kRowGap * s;

    const float labelHeight = kLabelHeight * s;
    layout.percentLabel = D2D1::RectF(cx0, y, cx1, y + labelHeight);
    y += labelHeight + kRowGap * s;

    const float muteWidth = 30.0f * s;
    const float muteHeight = kMuteHeight * s;
    mute.SetBounds(D2D1::RectF(left + (cardWidth - muteWidth) / 2.0f, y,
                                left + (cardWidth + muteWidth) / 2.0f, y + muteHeight));
    y += muteHeight + kRowGap * s;

    auto layoutPicker = [&](controls::ComboBox* combo, D2D1_RECT_F& label)
    {
        if (!combo) return;
        label = D2D1::RectF(cx0, y, cx1, y + kDeviceLabelHeight * s);
        y += (kDeviceLabelHeight + kDeviceLabelGap) * s;
        combo->SetBounds(D2D1::RectF(cx0, y, cx1, y + kComboHeight * s));
        y += (kComboHeight + kRowGap) * s;
    };
    layoutPicker(outputCombo, layout.outputLabel);
    layoutPicker(inputCombo, layout.inputLabel);

    if (meter)
    {
        const float meterHeight = kMeterHeight * s;
        meter->SetBounds(D2D1::RectF(cx0, y, cx1, y + meterHeight));
        y += meterHeight + kRowGap * s;
    }

    const float nameHeight = kNameHeight * s;
    layout.nameLabel = D2D1::RectF(cx0, y, cx1, y + nameHeight);

    return layout;
}

void MainWindow::Render()
{
    auto* ctx = resources_->Context();
    resources_->BeginDraw();

    ctx->Clear(theme::kWindow);

    const float margin = kMargin * dpiScale_;

    ctx->DrawText(L"Output", 6, labelFormat_.Get(),
                  D2D1::RectF(margin, margin, margin + 50.0f * dpiScale_, margin + 24.0f * dpiScale_), subtleTextBrush_.Get());
    outputCombo_->Draw(ctx, comboFormat_.Get(), panelBrush_.Get(), borderBrush_.Get(), textBrush_.Get(), subtleTextBrush_.Get());

    ctx->DrawText(L"Input", 5, labelFormat_.Get(),
                  D2D1::RectF(margin, margin + 32.0f * dpiScale_, margin + 50.0f * dpiScale_, margin + 56.0f * dpiScale_), subtleTextBrush_.Get());
    inputCombo_->Draw(ctx, comboFormat_.Get(), panelBrush_.Get(), borderBrush_.Get(), textBrush_.Get(), subtleTextBrush_.Get());

    RECT client;
    GetClientRect(hwnd_, &client);
    const D2D1_RECT_F clip = D2D1::RectF(
        0.0f, masterLayout_.card.top - 4.0f,
        static_cast<float>(client.right), static_cast<float>(client.bottom));
    ctx->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    DrawStrip(masterLayout_, masterFader_, masterMute_, nullptr, nullptr, nullptr, L"Device", true, std::nullopt, false);

    for (auto& strip : strips_)
    {
        DrawStrip(strip.layout, strip.fader, strip.mute, &strip.meter,
                  strip.isSystemSounds ? nullptr : strip.outputCombo.get(),
                  strip.isSystemSounds ? nullptr : strip.inputCombo.get(),
                  strip.name, strip.active, strip.executablePath, strip.isSystemSounds, strip.hasOutputSession);
    }

    ctx->PopAxisAlignedClip();

    if (!resources_->EndDraw())
    {
        CreateBrushes();
        CreateTextFormats();
    }
}

void MainWindow::DrawStrip(const StripLayout& layout, controls::FaderControl& fader, controls::MuteToggle& mute,
                            controls::PeakMeter* meter, controls::ComboBox* outputCombo, controls::ComboBox* inputCombo,
                            const std::wstring& name, bool active,
                            const std::optional<std::wstring>& executablePath, bool isSystemSounds, bool hasOutputSession)
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

    ID2D1Bitmap* iconBitmap = (executablePath || isSystemSounds)
        ? iconLoader_.ForExecutable(ctx, executablePath, isSystemSounds)
        : nullptr;

    if (iconBitmap)
    {
        ctx->DrawBitmap(iconBitmap, layout.icon);
    }
    else
    {
        // Fallback when extraction fails (or for the master/"Device" strip,
        // which has no associated executable): a colored circle with the
        // name's initial, rather than leaving a blank hole in the card.
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
    }

    if (hasOutputSession) fader.Draw(ctx, trackBrush_.Get(), accentBrush_.Get(), accentBrush_.Get(), accentHoverBrush_.Get(), windowRingBrush_.Get());

    const int percent = static_cast<int>(std::lround(fader.Value() * 100.0));
    const std::wstring percentText = hasOutputSession ? std::to_wstring(percent) + L"%" : L"Input only";
    ctx->DrawText(percentText.c_str(), static_cast<UINT32>(percentText.size()), labelFormat_.Get(),
                  layout.percentLabel, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    if (hasOutputSession) mute.Draw(ctx, trackBrush_.Get(), textBrush_.Get(), mutedRedBrush_.Get());

    if (outputCombo)
    {
        ctx->DrawText(L"Output", 6, nameFormat_.Get(), layout.outputLabel, subtleTextBrush_.Get());
        outputCombo->Draw(ctx, stripComboFormat_.Get(), panelBrush_.Get(), borderBrush_.Get(), textBrush_.Get(), subtleTextBrush_.Get());
    }

    if (inputCombo)
    {
        ctx->DrawText(L"Input", 5, nameFormat_.Get(), layout.inputLabel, subtleTextBrush_.Get());
        inputCombo->Draw(ctx, stripComboFormat_.Get(), panelBrush_.Get(), borderBrush_.Get(), textBrush_.Get(), subtleTextBrush_.Get());
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
        if (!strip.isSystemSounds && strip.outputCombo->HitTest(p)) { strip.outputCombo->OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (!strip.isSystemSounds && strip.inputCombo->HitTest(p)) { strip.inputCombo->OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (strip.hasOutputSession && strip.mute.HitTest(p)) { strip.mute.OnClick(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (strip.hasOutputSession && strip.fader.OnLButtonDown(p)) { draggingFader_ = &strip.fader; InvalidateRect(hwnd_, nullptr, FALSE); return; }
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
    const float viewWidth = static_cast<float>(client.right - client.left) - 2.0f * kMargin * dpiScale_;

    const float maxScroll = std::max(0.0f, contentWidth_ - viewWidth);
    scrollOffsetX_ -= static_cast<float>(delta) / 120.0f * 60.0f * dpiScale_;
    scrollOffsetX_ = std::clamp(scrollOffsetX_, 0.0f, maxScroll);

    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::Poll()
{
    try
    {
        const auto snapshots = audioService_.Refresh();
        outputDevices_ = audioService_.ListOutputDevices();
        inputDevices_ = audioService_.ListInputDevices();
        SyncOutputDevices();
        SyncInputDevices();
        ReconcileSessions(snapshots);
        SyncMaster();
    }
    catch (const std::exception&)
    {
        // Typically no active render endpoint at all; leave the last known
        // state on screen rather than tearing down the poll loop over it.
    }
}

void MainWindow::AnimateMeters()
{
    for (auto& strip : strips_)
    {
        strip.meter.Advance(kMeterFrameSeconds);
    }
}

void MainWindow::SyncMaster()
{
    const float deviceScalar = audioService_.GetMasterVolume();
    const float currentScalar = VolumeCurve::ToScalar(masterFader_.Value());
    if (!masterFader_.IsDragging() && std::abs(currentScalar - deviceScalar) > kScalarEpsilon)
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

    const auto& devices = outputDevices_;
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

    const auto& devices = inputDevices_;
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
            if (draggingFader_ == &it->fader)
            {
                OnLButtonUp({});
                ReleaseCapture();
            }
            it = strips_.erase(it);
        }
        else
        {
            ++it;
        }
    }

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
    if (!snapshot.hasOutputSession && draggingFader_ == &strip.fader)
    {
        OnLButtonUp({});
        ReleaseCapture();
    }
    strip.pid = snapshot.pid;
    strip.name = snapshot.displayName;
    strip.active = snapshot.IsActive();
    strip.hasOutputSession = snapshot.hasOutputSession;
    strip.executablePath = snapshot.executablePath;
    strip.isSystemSounds = snapshot.isSystemSounds;
    strip.meter.SetLevel(snapshot.peakLevel);
    strip.mute.SetMuted(snapshot.isMuted);
    if (snapshot.outputDeviceKnown) strip.outputDeviceId = snapshot.outputDeviceId;
    if (snapshot.inputDeviceKnown) strip.inputDeviceId = snapshot.inputDeviceId;
    SyncStripDevices(strip, false);
    SyncStripDevices(strip, true);

    if (strip.routeRequestedAt && !snapshot.activeOutputDeviceIds.empty())
    {
        auto target = strip.outputDeviceId;
        if (!target)
        {
            const auto device = std::find_if(outputDevices_.begin(), outputDevices_.end(),
                [](const auto& d) { return d.isDefault; });
            if (device != outputDevices_.end()) target = device->id;
        }
        const bool switched = target && std::all_of(snapshot.activeOutputDeviceIds.begin(), snapshot.activeOutputDeviceIds.end(),
            [&](const auto& id) { return id == *target; });
        if (switched)
        {
            strip.routeRequestedAt = 0;
        }
        else if (GetTickCount64() - strip.routeRequestedAt >= 3000)
        {
            strip.routeRequestedAt = 0;
            strip.routeWarningPending = true;
            // A dialog pumps messages, so display it after reconciliation
            // has finished and no iterator into the strip list is in use.
            PostMessageW(hwnd_, kRouteWarningMessage, static_cast<WPARAM>(strip.rowId), 0);
        }
    }

    if (strip.inputRouteRequestedAt && !snapshot.activeInputDeviceIds.empty())
    {
        const bool switched = strip.inputDeviceId && std::all_of(snapshot.activeInputDeviceIds.begin(), snapshot.activeInputDeviceIds.end(),
            [&](const auto& id) { return id == *strip.inputDeviceId; });
        if (switched) strip.inputRouteRequestedAt = 0;
        else if (GetTickCount64() - strip.inputRouteRequestedAt >= 3000)
        {
            strip.inputRouteRequestedAt = 0;
            strip.inputRouteWarningPending = true;
            PostMessageW(hwnd_, kRouteWarningMessage, static_cast<WPARAM>(strip.rowId), 1);
        }
    }

    const float currentScalar = VolumeCurve::ToScalar(strip.fader.Value());
    if (!strip.fader.IsDragging() && std::abs(currentScalar - snapshot.volume) > kScalarEpsilon)
    {
        strip.fader.SetValue(VolumeCurve::ToPosition(snapshot.volume));
    }
}

void MainWindow::SyncStripDevices(ChannelStrip& strip, bool input)
{
    auto& combo = input ? strip.inputCombo : strip.outputCombo;
    if (strip.isSystemSounds || combo->IsOpen()) return;
    const auto& selectedDeviceId = input ? strip.inputDeviceId : strip.outputDeviceId;
    const auto& devices = input ? inputDevices_ : outputDevices_;

    std::vector<std::wstring> items{L"Default"};
    std::vector<std::wstring> deviceIds;
    for (const auto& device : devices)
    {
        items.push_back(device.friendlyName);
        deviceIds.push_back(device.id);
    }

    int newIndex = 0;
    if (selectedDeviceId)
    {
        const auto it = std::find(deviceIds.begin(), deviceIds.end(), *selectedDeviceId);
        if (it != deviceIds.end())
        {
            newIndex = static_cast<int>(std::distance(deviceIds.begin(), it)) + 1;
        }
        else
        {
            items.push_back(L"Unavailable device");
            deviceIds.push_back(*selectedDeviceId);
            newIndex = static_cast<int>(deviceIds.size());
        }
    }

    (input ? strip.inputDeviceIds : strip.outputDeviceIds) = std::move(deviceIds);
    auto compactLabels = CompactDeviceLabels(items);
    combo->SetItems(std::move(items), std::move(compactLabels));
    combo->SetSelectedIndex(newIndex);
}

void MainWindow::ChangeStripDevice(uint64_t rowId, int index, bool input)
{
    const auto it = std::find_if(strips_.begin(), strips_.end(),
        [&](const ChannelStrip& strip) { return strip.rowId == rowId; });
    if (it == strips_.end()) return;
    const auto& ids = input ? it->inputDeviceIds : it->outputDeviceIds;
    if (index < 0 || index > static_cast<int>(ids.size())) return;
    const std::optional<std::wstring> deviceId = index ? std::optional<std::wstring>(ids[index - 1]) : std::nullopt;
    try
    {
        if (input)
        {
            it->inputRouteWarningPending = false;
            audioService_.SetAppInputDevice(it->instanceId, deviceId);
            it->inputDeviceId = deviceId;
            it->inputRouteRequestedAt = deviceId ? GetTickCount64() : 0;
        }
        else
        {
            it->routeWarningPending = false;
            audioService_.SetAppOutputDevice(it->instanceId, deviceId);
            it->outputDeviceId = deviceId;
            it->routeRequestedAt = GetTickCount64();
        }
    }
    catch (const std::exception& ex)
    {
        SyncStripDevices(*it, input);
        const std::string message = ex.what();
        const std::wstring detail(message.begin(), message.end());
        MessageBoxW(hwnd_, detail.c_str(), input ? L"Could not switch input" : L"Could not switch output", MB_OK | MB_ICONERROR);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

ChannelStrip MainWindow::CreateStrip(const winmix::audio::AudioSessionSnapshot& snapshot)
{
    ChannelStrip strip;
    strip.rowId = nextRowId_++;
    strip.instanceId = snapshot.instanceId;
    strip.pid = snapshot.pid;
    strip.name = snapshot.displayName;
    strip.active = snapshot.IsActive();
    strip.hasOutputSession = snapshot.hasOutputSession;
    strip.executablePath = snapshot.executablePath;
    strip.isSystemSounds = snapshot.isSystemSounds;
    strip.fader.SetValue(VolumeCurve::ToPosition(snapshot.volume));
    strip.mute.SetMuted(snapshot.isMuted);
    strip.meter.SetLevel(snapshot.peakLevel);

    strip.outputCombo = std::make_unique<controls::ComboBox>(hwnd_);
    strip.inputCombo = std::make_unique<controls::ComboBox>(hwnd_);
    strip.outputDeviceId = snapshot.outputDeviceId;
    strip.inputDeviceId = snapshot.inputDeviceId;
    SyncStripDevices(strip, false);
    SyncStripDevices(strip, true);

    const uint64_t rowId = strip.rowId;
    strip.outputCombo->onChange = [this, rowId](int index) { ChangeStripDevice(rowId, index, false); };
    strip.inputCombo->onChange = [this, rowId](int index) { ChangeStripDevice(rowId, index, true); };

    strip.fader.onChange = [this, rowId](double position)
    {
        const auto it = std::find_if(strips_.begin(), strips_.end(),
                                      [&](const ChannelStrip& s) { return s.rowId == rowId; });
        if (it == strips_.end())
        {
            return;
        }
        audioService_.SetVolume(it->instanceId, VolumeCurve::ToScalar(position));
    };
    strip.mute.onChange = [this, rowId](bool muted)
    {
        const auto it = std::find_if(strips_.begin(), strips_.end(),
                                      [&](const ChannelStrip& s) { return s.rowId == rowId; });
        if (it == strips_.end())
        {
            return;
        }
        audioService_.SetMute(it->instanceId, muted);
    };

    return strip;
}

} // namespace winmix::app
