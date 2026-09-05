#pragma once

#include <d2d1_1.h>
#include <functional>

namespace winmix::app::controls {

// A vertical fader: a 4px rounded track, an accent-filled rounded segment
// from the bottom up to the thumb, and a 14px circular thumb with a 2px
// dark ring stroke. Bounds() is the whole clickable column (matching the
// original's Width=24, IsMoveToPointEnabled=True slider), not just the
// visual track -- clicking anywhere in it jumps the thumb there.
class FaderControl
{
public:
    void SetBounds(D2D1_RECT_F bounds) { bounds_ = bounds; }
    const D2D1_RECT_F& Bounds() const { return bounds_; }

    // DPI scale factor (GetDpiForWindow()/96) for the track width and thumb
    // radius, which are drawn as fixed-DIP constants rather than proportions
    // of bounds_. bounds_ itself is already scaled by the caller (Layout()).
    void SetScale(float scale) { scale_ = scale; }

    double Value() const { return value_; }

    // Programmatic set (e.g. from a poll or fake data) -- does not fire onChange.
    void SetValue(double value);

    void Draw(ID2D1DeviceContext* ctx,
              ID2D1SolidColorBrush* trackBrush, ID2D1SolidColorBrush* fillBrush,
              ID2D1SolidColorBrush* thumbBrush, ID2D1SolidColorBrush* thumbHoverBrush,
              ID2D1SolidColorBrush* ringBrush) const;

    bool HitTest(D2D1_POINT_2F pt) const;

    // Returns true if this control started (or continues) capturing a drag.
    bool OnLButtonDown(D2D1_POINT_2F pt);
    void OnMouseMove(D2D1_POINT_2F pt);
    void OnLButtonUp();

    bool IsDragging() const { return dragging_; }

    std::function<void(double)> onChange; // fired only for user-driven changes

private:
    double PositionFromPoint(float y) const;

    D2D1_RECT_F bounds_{};
    float scale_ = 1.0f;
    double value_ = 0.0;
    bool dragging_ = false;
    bool hovered_ = false;
};

} // namespace winmix::app::controls
