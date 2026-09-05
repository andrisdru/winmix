#include "winmix/audio/VolumeCurve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace winmix::audio {

namespace {

void EnsureNegative(double minDb)
{
    if (minDb >= 0.0 || std::isnan(minDb))
    {
        throw std::invalid_argument("The floor must be a negative dB value.");
    }
}

} // namespace

float VolumeCurve::ToScalar(double position, double minDb)
{
    EnsureNegative(minDb);
    position = std::clamp(position, 0.0, 1.0);

    // Position 0 must be true silence, not just minDb, or muting via the
    // slider would leave a faint audible tail.
    if (position <= 0.0)
    {
        return 0.0f;
    }

    const double db = minDb * (1.0 - position);
    return static_cast<float>(std::clamp(DbToScalar(db), 0.0, 1.0));
}

double VolumeCurve::ToPosition(double scalar, double minDb)
{
    EnsureNegative(minDb);
    scalar = std::clamp(scalar, 0.0, 1.0);

    if (scalar <= 0.0)
    {
        return 0.0;
    }

    const double db = ScalarToDb(scalar);
    if (db <= minDb)
    {
        return 0.0;
    }

    return std::clamp(1.0 - (db / minDb), 0.0, 1.0);
}

double VolumeCurve::ScalarToDb(double scalar)
{
    return scalar <= 0.0 ? -std::numeric_limits<double>::infinity() : 20.0 * std::log10(scalar);
}

double VolumeCurve::DbToScalar(double db)
{
    return std::pow(10.0, db / 20.0);
}

} // namespace winmix::audio
