#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "render/DeviceResources.h"
#include "controls/FaderControl.h"
#include "controls/PeakMeter.h"
#include "controls/MuteToggle.h"
#include "controls/ComboBox.h"
#include "IconLoader.h"
#include "TrayIcon.h"

#include "winmix/audio/AudioSessionService.h"

namespace winmix::app {

// Precomputed per-frame geometry for one channel strip's non-control
// elements (card background, icon placeholder, text labels), filled by
// MainWindow::LayoutStrip and consumed by MainWindow::DrawStrip.
struct StripLayout
{
    D2D1_RECT_F card{};
    D2D1_RECT_F icon{};
    D2D1_RECT_F percentLabel{};
    D2D1_RECT_F nameLabel{};
};

// One app's channel strip. Keyed by instanceId (matching
// AudioSessionSnapshot::instanceId) across polls -- two windows of the same
// app share a session identifier but get distinct instance ids, which is
// exactly why this, not the display name or pid, is the reconciliation key.
struct ChannelStrip
{
    std::wstring instanceId;
    uint32_t pid = 0;
    std::wstring name;
    bool active = false;
    std::optional<std::wstring> executablePath;
    bool isSystemSounds = false;

    controls::FaderControl fader;
    controls::MuteToggle mute;
    controls::PeakMeter meter;
    std::unique_ptr<controls::ComboBox> outputCombo;
    StripLayout layout;
};

class MainWindow
{
public:
    explicit MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Show(int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateBrushes();
    void CreateTextFormats();
    void Layout();
    StripLayout LayoutStrip(float left, float top, float height,
                             controls::FaderControl& fader, controls::MuteToggle& mute,
                             controls::PeakMeter* meter, controls::ComboBox* outputCombo);
    void Render();
    void DrawStrip(const StripLayout& layout, controls::FaderControl& fader, controls::MuteToggle& mute,
                    controls::PeakMeter* meter, controls::ComboBox* outputCombo,
                    const std::wstring& name, bool active,
                    const std::optional<std::wstring>& executablePath, bool isSystemSounds);

    void OnLButtonDown(POINT pt);
    void OnMouseMove(POINT pt);
    void OnLButtonUp(POINT pt);
    void OnMouseWheel(int delta);

    // Poll-loop plumbing (WM_TIMER-driven, ~100ms -- see AudioSessionService's
    // own threading contract). Discovery is poll-based, not event-based, for
    // the same reason the .NET version is: WASAPI's session-notification
    // callbacks arrive off-thread and re-entering the session manager from
    // one deadlocks.
    void Poll();
    void SyncMaster();
    void SyncOutputDevices();
    void SyncInputDevices();
    void ReconcileSessions(const std::vector<winmix::audio::AudioSessionSnapshot>& snapshots);
    void SyncStrip(ChannelStrip& strip, const winmix::audio::AudioSessionSnapshot& snapshot);
    ChannelStrip CreateStrip(const winmix::audio::AudioSessionSnapshot& snapshot);

    // Tray-driven lifecycle: polling only runs while the window is visible
    // (worth it -- there is nothing to animate while hidden, so the COM
    // traffic would be pure waste), matching AudioSessionService.Stop()/
    // Start() in the .NET port.
    void ShowMixer();
    void StartPolling();
    void StopPolling();
    std::vector<TrayInputDevice> ListInputDevicesForTray();

    HWND hwnd_ = nullptr;
    std::unique_ptr<render::DeviceResources> resources_;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> subtleTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentHoverBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> meterBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedRedBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> windowRingBrush_;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> nameFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> comboFormat_;

    winmix::audio::AudioSessionService audioService_;
    IconLoader iconLoader_;
    std::unique_ptr<TrayIcon> tray_;
    bool timerRunning_ = false;

    controls::FaderControl masterFader_;
    controls::MuteToggle masterMute_;
    StripLayout masterLayout_;
    std::unique_ptr<controls::ComboBox> outputCombo_;
    std::unique_ptr<controls::ComboBox> inputCombo_;
    // outputCombo_/inputCombo_ items map 1:1 to these (no "Default" sentinel
    // at the header level -- one of these is always the actual default).
    std::vector<std::wstring> outputDeviceIds_;
    std::vector<std::wstring> inputDeviceIds_;
    std::vector<ChannelStrip> strips_;

    float scrollOffsetX_ = 0.0f;
    float contentWidth_ = 0.0f;

    // The fader currently owning a mouse drag, if any.
    controls::FaderControl* draggingFader_ = nullptr;
};

} // namespace winmix::app
