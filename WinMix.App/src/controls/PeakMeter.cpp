#include "controls/PeakMeter.h"

#include <algorithm>

namespace winmix::app::controls {

namespace {
// Per-second rate limit on the displayed level. Attack is much faster than
// decay -- transients still register almost immediately, while the
// fall-off glides instead of stepping down once per 100ms poll.
constexpr float kAttackPerSecond = 12.0f;
constexpr float kDecayPerSecond = 4.0f;
} // namespace

void PeakMeter::SetLevel(float level)
{
    targetLevel_ = std::clamp(level, 0.0f, 1.0f);
}

void PeakMeter::Advance(float deltaSeconds)
{
    const float diff = targetLevel_ - displayLevel_;
    const float rate = diff >= 0.0f ? kAttackPerSecond : kDecayPerSecond;
    const float maxStep = rate * deltaSeconds;
    displayLevel_ += std::clamp(diff, -maxStep, maxStep);
}

void PeakMeter::Draw(ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* trackBrush, ID2D1SolidColorBrush* meterBrush) const
{
    const float radius = (bounds_.bottom - bounds_.top) / 2.0f;
    ctx->FillRoundedRectangle(D2D1::RoundedRect(bounds_, radius, radius), trackBrush);

    if (displayLevel_ > 0.0f)
    {
        const float width = (bounds_.right - bounds_.left) * displayLevel_;
        const D2D1_RECT_F fillRect = D2D1::RectF(bounds_.left, bounds_.top, bounds_.left + width, bounds_.bottom);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(fillRect, radius, radius), meterBrush);
    }
}

} // namespace winmix::app::controls
