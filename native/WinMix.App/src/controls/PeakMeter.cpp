#include "controls/PeakMeter.h"

#include <algorithm>

namespace winmix::app::controls {

void PeakMeter::SetLevel(float level)
{
    level_ = std::clamp(level, 0.0f, 1.0f);
}

void PeakMeter::Draw(ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* trackBrush, ID2D1SolidColorBrush* meterBrush) const
{
    const float radius = (bounds_.bottom - bounds_.top) / 2.0f;
    ctx->FillRoundedRectangle(D2D1::RoundedRect(bounds_, radius, radius), trackBrush);

    if (level_ > 0.0f)
    {
        const float width = (bounds_.right - bounds_.left) * level_;
        const D2D1_RECT_F fillRect = D2D1::RectF(bounds_.left, bounds_.top, bounds_.left + width, bounds_.bottom);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(fillRect, radius, radius), meterBrush);
    }
}

} // namespace winmix::app::controls
