#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>

#include <functional>

namespace winmix::app::controls {

// A 30x26 mute button, hand-drawn (a speaker glyph, struck through when
// muted) rather than relying on an emoji font -- avoids a color-font/glyph
// fallback dependency for a native build. WPF got the speaker/no-speaker
// emoji for free; DirectWrite color-font rendering is not guaranteed to the
// same degree, so this sidesteps it entirely.
class MuteToggle
{
public:
    void SetBounds(D2D1_RECT_F bounds) { bounds_ = bounds; }
    const D2D1_RECT_F& Bounds() const { return bounds_; }

    bool IsMuted() const { return muted_; }
    void SetMuted(bool muted) { muted_ = muted; } // programmatic, no onChange

    bool HitTest(D2D1_POINT_2F pt) const;
    void OnMouseMove(D2D1_POINT_2F pt) { hovered_ = HitTest(pt); }
    void OnClick(D2D1_POINT_2F pt);

    void Draw(ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* chromeHoverBrush,
              ID2D1SolidColorBrush* glyphBrush, ID2D1SolidColorBrush* mutedGlyphBrush) const;

    std::function<void(bool)> onChange;

private:
    D2D1_RECT_F bounds_{};
    bool muted_ = false;
    bool hovered_ = false;
    mutable Microsoft::WRL::ComPtr<ID2D1PathGeometry> speakerGeometry_;
};

} // namespace winmix::app::controls
