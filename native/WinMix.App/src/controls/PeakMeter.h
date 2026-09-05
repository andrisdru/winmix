#pragma once

#include <d2d1_1.h>

namespace winmix::app::controls {

// A thin two-tone bar: a rounded track background plus a filled rounded
// segment proportional to the current level, left-aligned.
class PeakMeter
{
public:
    void SetBounds(D2D1_RECT_F bounds) { bounds_ = bounds; }
    void SetLevel(float level);

    void Draw(ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* trackBrush, ID2D1SolidColorBrush* meterBrush) const;

private:
    D2D1_RECT_F bounds_{};
    float level_ = 0.0f;
};

} // namespace winmix::app::controls
