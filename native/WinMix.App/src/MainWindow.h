#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <string>
#include <vector>

#include "render/DeviceResources.h"
#include "controls/FaderControl.h"
#include "controls/PeakMeter.h"
#include "controls/MuteToggle.h"
#include "controls/ComboBox.h"

namespace winmix::app {

// One fake channel strip's data, standing in for a real
// winmix::audio::AudioSessionSnapshot until stage 3 wires the real engine
// into this poll-and-draw loop.
struct FakeSession
{
    std::wstring name;
    double volume = 0.5;
    bool muted = false;
    float peak = 0.0f;
    bool active = true;
};

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

struct ChannelStrip
{
    controls::FaderControl fader;
    controls::MuteToggle mute;
    controls::PeakMeter meter;
    std::unique_ptr<controls::ComboBox> outputCombo;
    FakeSession data;
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
                    const std::wstring& name, bool active);

    void OnLButtonDown(POINT pt);
    void OnMouseMove(POINT pt);
    void OnLButtonUp(POINT pt);
    void OnMouseWheel(int delta);

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

    controls::FaderControl masterFader_;
    controls::MuteToggle masterMute_;
    StripLayout masterLayout_;
    std::unique_ptr<controls::ComboBox> outputCombo_;
    std::unique_ptr<controls::ComboBox> inputCombo_;
    std::vector<ChannelStrip> strips_;

    float scrollOffsetX_ = 0.0f;
    float contentWidth_ = 0.0f;

    // The fader currently owning a mouse drag, if any.
    controls::FaderControl* draggingFader_ = nullptr;
};

} // namespace winmix::app
