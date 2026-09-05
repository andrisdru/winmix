#include "controls/FaderControl.h"

#include <algorithm>

namespace winmix::app::controls {

namespace {
constexpr float kTrackHalfWidth = 2.0f;
constexpr float kThumbRadius = 7.0f;
} // namespace

void FaderControl::SetValue(double value)
{
    value_ = std::clamp(value, 0.0, 1.0);
}

double FaderControl::PositionFromPoint(float y) const
{
    const float travelTop = bounds_.top + kThumbRadius;
    const float travelBottom = bounds_.bottom - kThumbRadius;
    if (travelBottom <= travelTop)
    {
        return 0.0;
    }

    const double position = (travelBottom - y) / (travelBottom - travelTop);
    return std::clamp(position, 0.0, 1.0);
}

bool FaderControl::HitTest(D2D1_POINT_2F pt) const
{
    return pt.x >= bounds_.left && pt.x <= bounds_.right && pt.y >= bounds_.top && pt.y <= bounds_.bottom;
}

bool FaderControl::OnLButtonDown(D2D1_POINT_2F pt)
{
    if (!HitTest(pt))
    {
        return false;
    }

    dragging_ = true;
    value_ = PositionFromPoint(pt.y);
    if (onChange)
    {
        onChange(value_);
    }
    return true;
}

void FaderControl::OnMouseMove(D2D1_POINT_2F pt)
{
    hovered_ = HitTest(pt);
    if (dragging_)
    {
        value_ = PositionFromPoint(pt.y);
        if (onChange)
        {
            onChange(value_);
        }
    }
}

void FaderControl::OnLButtonUp()
{
    dragging_ = false;
}

void FaderControl::Draw(ID2D1DeviceContext* ctx,
                         ID2D1SolidColorBrush* trackBrush, ID2D1SolidColorBrush* fillBrush,
                         ID2D1SolidColorBrush* thumbBrush, ID2D1SolidColorBrush* thumbHoverBrush,
                         ID2D1SolidColorBrush* ringBrush) const
{
    const float centerX = (bounds_.left + bounds_.right) / 2.0f;

    const D2D1_ROUNDED_RECT trackRect = D2D1::RoundedRect(
        D2D1::RectF(centerX - kTrackHalfWidth, bounds_.top, centerX + kTrackHalfWidth, bounds_.bottom),
        kTrackHalfWidth, kTrackHalfWidth);
    ctx->FillRoundedRectangle(trackRect, trackBrush);

    const float travelTop = bounds_.top + kThumbRadius;
    const float travelBottom = bounds_.bottom - kThumbRadius;
    const float thumbY = travelBottom - static_cast<float>(value_) * (travelBottom - travelTop);

    if (thumbY < travelBottom)
    {
        const D2D1_ROUNDED_RECT fillRect = D2D1::RoundedRect(
            D2D1::RectF(centerX - kTrackHalfWidth, thumbY, centerX + kTrackHalfWidth, bounds_.bottom),
            kTrackHalfWidth, kTrackHalfWidth);
        ctx->FillRoundedRectangle(fillRect, fillBrush);
    }

    ID2D1SolidColorBrush* knobBrush = (hovered_ || dragging_) ? thumbHoverBrush : thumbBrush;
    const D2D1_ELLIPSE knob = D2D1::Ellipse(D2D1::Point2F(centerX, thumbY), kThumbRadius, kThumbRadius);
    ctx->FillEllipse(knob, knobBrush);
    ctx->DrawEllipse(knob, ringBrush, 2.0f);
}

} // namespace winmix::app::controls
