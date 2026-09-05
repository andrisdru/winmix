#pragma once

namespace winmix::audio {

// Maps between a linear slider position and the linear amplitude scalar that
// WASAPI's ISimpleAudioVolume expects. WASAPI takes amplitude, but loudness
// is perceived roughly logarithmically, so slider travel is treated as
// linear in decibels across kDefaultMinDb..0 dB (what the Windows mixer
// effectively does) rather than linear in amplitude.
class VolumeCurve
{
public:
    static constexpr double kDefaultMinDb = -60.0;

    // Slider position (0..1) to WASAPI amplitude scalar (0..1).
    static float ToScalar(double position, double minDb = kDefaultMinDb);

    // WASAPI amplitude scalar (0..1) back to slider position (0..1).
    static double ToPosition(double scalar, double minDb = kDefaultMinDb);

    static double ScalarToDb(double scalar);
    static double DbToScalar(double db);
};

} // namespace winmix::audio
