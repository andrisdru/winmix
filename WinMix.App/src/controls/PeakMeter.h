#pragma once

#include <d2d1_1.h>

namespace winmix::app::controls {

// A thin two-tone bar: a rounded track background plus a filled rounded
// segment proportional to the current level, left-aligned.
class PeakMeter
{
public:
    void SetBounds(D2D1_RECT_F bounds) { bounds_ = bounds; }

    // Records the latest polled level as the target. The visible bar does
    // not jump here -- it eases toward this via Advance(), since the poll
    // driving this only fires every 100ms and a hard snap between calls
    // reads as choppy.
    void SetLevel(float level);

    // Moves the displayed level toward the last SetLevel target by up to
    // one ballistics-limited step. Call this from a faster animation tick
    // than the audio poll so motion between polled values reads as
    // continuous rather than stepped.
    void Advance(float deltaSeconds);

    void Draw(ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* trackBrush, ID2D1SolidColorBrush* meterBrush) const;

private:
    D2D1_RECT_F bounds_{};
    float targetLevel_ = 0.0f;
    float displayLevel_ = 0.0f;
};

} // namespace winmix::app::controls
