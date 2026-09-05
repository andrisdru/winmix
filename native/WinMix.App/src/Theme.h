#pragma once

#include <windows.h>
#include <d2d1.h>

#include <cstdint>

namespace winmix::app::theme {

constexpr D2D1_COLOR_F FromArgb(uint32_t argb)
{
    return D2D1_COLOR_F{
        ((argb >> 16) & 0xFF) / 255.0f,
        ((argb >> 8) & 0xFF) / 255.0f,
        (argb & 0xFF) / 255.0f,
        ((argb >> 24) & 0xFF) / 255.0f};
}

inline COLORREF ToColorRef(const D2D1_COLOR_F& c)
{
    return RGB(
        static_cast<BYTE>(c.r * 255.0f + 0.5f),
        static_cast<BYTE>(c.g * 255.0f + 0.5f),
        static_cast<BYTE>(c.b * 255.0f + 0.5f));
}

inline constexpr D2D1_COLOR_F kWindow = FromArgb(0xFF1B1B1F);
inline constexpr D2D1_COLOR_F kPanel = FromArgb(0xFF25252B);
inline constexpr D2D1_COLOR_F kBorder = FromArgb(0xFF35353D);
inline constexpr D2D1_COLOR_F kText = FromArgb(0xFFF2F2F5);
inline constexpr D2D1_COLOR_F kSubtleText = FromArgb(0xFF9A9AA6);
inline constexpr D2D1_COLOR_F kAccent = FromArgb(0xFF4CC2FF);
inline constexpr D2D1_COLOR_F kAccentHover = FromArgb(0xFF8AD9FF);
inline constexpr D2D1_COLOR_F kTrack = FromArgb(0xFF3A3A44);
inline constexpr D2D1_COLOR_F kMeter = FromArgb(0xFF4ADE80);
inline constexpr D2D1_COLOR_F kMutedRed = FromArgb(0xFFFF6B6B);
inline constexpr D2D1_COLOR_F kStatusText = FromArgb(0xFFFFB4A2);

} // namespace winmix::app::theme
