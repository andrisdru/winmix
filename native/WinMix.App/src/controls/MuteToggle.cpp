#include "controls/MuteToggle.h"

using Microsoft::WRL::ComPtr;

namespace winmix::app::controls {

bool MuteToggle::HitTest(D2D1_POINT_2F pt) const
{
    return pt.x >= bounds_.left && pt.x <= bounds_.right && pt.y >= bounds_.top && pt.y <= bounds_.bottom;
}

void MuteToggle::OnClick(D2D1_POINT_2F pt)
{
    if (!HitTest(pt))
    {
        return;
    }

    muted_ = !muted_;
    if (onChange)
    {
        onChange(muted_);
    }
}

void MuteToggle::Draw(ID2D1DeviceContext* ctx, ID2D1SolidColorBrush* chromeHoverBrush,
                       ID2D1SolidColorBrush* glyphBrush, ID2D1SolidColorBrush* mutedGlyphBrush) const
{
    if (hovered_)
    {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(bounds_, 4.0f, 4.0f), chromeHoverBrush);
    }

    ComPtr<ID2D1Factory> factory;
    ctx->GetFactory(&factory);

    if (!speakerGeometry_)
    {
        factory->CreatePathGeometry(&speakerGeometry_);
        ComPtr<ID2D1GeometrySink> sink;
        speakerGeometry_->Open(&sink);
        sink->BeginFigure(D2D1::Point2F(-7.0f, -3.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(-3.0f, -3.0f));
        sink->AddLine(D2D1::Point2F(2.0f, -7.0f));
        sink->AddLine(D2D1::Point2F(2.0f, 7.0f));
        sink->AddLine(D2D1::Point2F(-3.0f, 3.0f));
        sink->AddLine(D2D1::Point2F(-7.0f, 3.0f));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
    }

    const float cx = (bounds_.left + bounds_.right) / 2.0f - 3.0f;
    const float cy = (bounds_.top + bounds_.bottom) / 2.0f;

    ID2D1SolidColorBrush* brush = muted_ ? mutedGlyphBrush : glyphBrush;

    ComPtr<ID2D1TransformedGeometry> transformed;
    factory->CreateTransformedGeometry(speakerGeometry_.Get(), D2D1::Matrix3x2F::Translation(cx, cy), &transformed);
    ctx->FillGeometry(transformed.Get(), brush);

    if (muted_)
    {
        ctx->DrawLine(D2D1::Point2F(cx - 2.0f, cy - 8.0f), D2D1::Point2F(cx + 9.0f, cy + 8.0f), mutedGlyphBrush, 2.0f);
    }
    else
    {
        ctx->DrawLine(D2D1::Point2F(cx + 5.0f, cy - 5.0f), D2D1::Point2F(cx + 9.0f, cy - 8.0f), glyphBrush, 1.5f);
        ctx->DrawLine(D2D1::Point2F(cx + 5.0f, cy + 5.0f), D2D1::Point2F(cx + 9.0f, cy + 8.0f), glyphBrush, 1.5f);
    }
}

} // namespace winmix::app::controls
