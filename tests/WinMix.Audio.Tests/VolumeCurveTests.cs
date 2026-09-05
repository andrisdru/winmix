using WinMix.Audio;
using Xunit;

namespace WinMix.Audio.Tests;

public class VolumeCurveTests
{
    [Fact]
    public void ZeroPositionIsTrueSilence()
    {
        // Not just the dB floor: a slider pulled to the bottom must be inaudible.
        Assert.Equal(0f, VolumeCurve.ToScalar(0.0));
    }

    [Fact]
    public void FullPositionIsUnityGain()
    {
        Assert.Equal(1f, VolumeCurve.ToScalar(1.0), precision: 5);
    }

    [Fact]
    public void MidPositionSitsWellBelowHalfAmplitude()
    {
        // The whole point of the taper: half travel is roughly -30 dB, not 0.5
        // amplitude. If this ever reads ~0.5 the curve has been bypassed.
        var scalar = VolumeCurve.ToScalar(0.5);

        Assert.InRange(scalar, 0.01f, 0.1f);
    }

    [Theory]
    [InlineData(0.05)]
    [InlineData(0.25)]
    [InlineData(0.5)]
    [InlineData(0.75)]
    [InlineData(1.0)]
    public void PositionSurvivesRoundTrip(double position)
    {
        var restored = VolumeCurve.ToPosition(VolumeCurve.ToScalar(position));

        Assert.Equal(position, restored, precision: 4);
    }

    [Fact]
    public void CurveIsMonotonic()
    {
        var previous = -1f;

        for (var position = 0.0; position <= 1.0; position += 0.01)
        {
            var scalar = VolumeCurve.ToScalar(position);
            Assert.True(scalar >= previous, $"Curve dipped at position {position}.");
            previous = scalar;
        }
    }

    [Theory]
    [InlineData(-0.5, 0f)]
    [InlineData(1.5, 1f)]
    public void PositionsOutsideUnitRangeAreClamped(double position, float expected)
    {
        Assert.Equal(expected, VolumeCurve.ToScalar(position), precision: 5);
    }

    [Fact]
    public void ScalarsBelowTheFloorCollapseToZeroPosition()
    {
        var belowFloor = VolumeCurve.DbToScalar(VolumeCurve.DefaultMinDb - 10.0);

        Assert.Equal(0.0, VolumeCurve.ToPosition(belowFloor));
    }

    [Fact]
    public void ASteeperFloorPushesTheSamePositionQuieter()
    {
        var gentle = VolumeCurve.ToScalar(0.5, minDb: -30.0);
        var steep = VolumeCurve.ToScalar(0.5, minDb: -90.0);

        Assert.True(steep < gentle);
    }

    [Theory]
    [InlineData(0.0)]
    [InlineData(6.0)]
    [InlineData(double.NaN)]
    public void ANonNegativeFloorIsRejected(double minDb)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => VolumeCurve.ToScalar(0.5, minDb));
        Assert.Throws<ArgumentOutOfRangeException>(() => VolumeCurve.ToPosition(0.5, minDb));
    }

    [Fact]
    public void SilenceHasNoDecibelValue()
    {
        Assert.Equal(double.NegativeInfinity, VolumeCurve.ScalarToDb(0.0));
    }

    [Theory]
    [InlineData(0.0, 1.0)]
    [InlineData(-6.0, 0.501)]
    [InlineData(-20.0, 0.1)]
    public void DecibelsConvertToTheExpectedAmplitude(double db, double expected)
    {
        Assert.Equal(expected, VolumeCurve.DbToScalar(db), precision: 3);
    }
}
