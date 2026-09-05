#include "doctest.h"
#include "winmix/audio/VolumeCurve.h"

#include <limits>
#include <stdexcept>

using winmix::audio::VolumeCurve;

TEST_CASE("ZeroPositionIsTrueSilence")
{
    // Not just the dB floor: a slider pulled to the bottom must be inaudible.
    CHECK(VolumeCurve::ToScalar(0.0) == 0.0f);
}

TEST_CASE("FullPositionIsUnityGain")
{
    CHECK(VolumeCurve::ToScalar(1.0) == doctest::Approx(1.0f).epsilon(0.00001));
}

TEST_CASE("MidPositionSitsWellBelowHalfAmplitude")
{
    // The whole point of the taper: half travel is roughly -30 dB, not 0.5
    // amplitude. If this ever reads ~0.5 the curve has been bypassed.
    const float scalar = VolumeCurve::ToScalar(0.5);

    CHECK(scalar >= 0.01f);
    CHECK(scalar <= 0.1f);
}

TEST_CASE("PositionSurvivesRoundTrip")
{
    for (const double position : {0.05, 0.25, 0.5, 0.75, 1.0})
    {
        const double restored = VolumeCurve::ToPosition(VolumeCurve::ToScalar(position));
        CHECK(restored == doctest::Approx(position).epsilon(0.0001));
    }
}

TEST_CASE("CurveIsMonotonic")
{
    float previous = -1.0f;

    for (double position = 0.0; position <= 1.0; position += 0.01)
    {
        const float scalar = VolumeCurve::ToScalar(position);
        CHECK_MESSAGE(scalar >= previous, "Curve dipped at position " << position);
        previous = scalar;
    }
}

TEST_CASE("PositionsOutsideUnitRangeAreClamped")
{
    CHECK(VolumeCurve::ToScalar(-0.5) == doctest::Approx(0.0f));
    CHECK(VolumeCurve::ToScalar(1.5) == doctest::Approx(1.0f));
}

TEST_CASE("ScalarsBelowTheFloorCollapseToZeroPosition")
{
    const double belowFloor = VolumeCurve::DbToScalar(VolumeCurve::kDefaultMinDb - 10.0);
    CHECK(VolumeCurve::ToPosition(belowFloor) == 0.0);
}

TEST_CASE("ASteeperFloorPushesTheSamePositionQuieter")
{
    const float gentle = VolumeCurve::ToScalar(0.5, -30.0);
    const float steep = VolumeCurve::ToScalar(0.5, -90.0);
    CHECK(steep < gentle);
}

TEST_CASE("ANonNegativeFloorIsRejected")
{
    for (const double minDb : {0.0, 6.0, std::numeric_limits<double>::quiet_NaN()})
    {
        CHECK_THROWS_AS(VolumeCurve::ToScalar(0.5, minDb), std::invalid_argument);
        CHECK_THROWS_AS(VolumeCurve::ToPosition(0.5, minDb), std::invalid_argument);
    }
}

TEST_CASE("SilenceHasNoDecibelValue")
{
    CHECK(VolumeCurve::ScalarToDb(0.0) == -std::numeric_limits<double>::infinity());
}

TEST_CASE("DecibelsConvertToTheExpectedAmplitude")
{
    CHECK(VolumeCurve::DbToScalar(0.0) == doctest::Approx(1.0).epsilon(0.001));
    CHECK(VolumeCurve::DbToScalar(-6.0) == doctest::Approx(0.501).epsilon(0.001));
    CHECK(VolumeCurve::DbToScalar(-20.0) == doctest::Approx(0.1).epsilon(0.001));
}
