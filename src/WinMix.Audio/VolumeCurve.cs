namespace WinMix.Audio;

/// <summary>
/// Maps between a linear slider position and the linear amplitude scalar that
/// WASAPI's <c>ISimpleAudioVolume::SetMasterVolume</c> expects.
///
/// The distinction matters: WASAPI takes amplitude, but loudness is perceived
/// roughly logarithmically. Feeding a slider position straight through makes the
/// bottom two thirds of travel sound almost identical and the top third violent.
/// We therefore treat slider travel as linear in decibels across
/// <see cref="DefaultMinDb"/>..0 dB, which is what the Windows mixer effectively does.
/// </summary>
public static class VolumeCurve
{
    /// <summary>Amplitude below which we treat a session as silent.</summary>
    public const double DefaultMinDb = -60.0;

    /// <summary>Slider position (0..1) to WASAPI amplitude scalar (0..1).</summary>
    public static float ToScalar(double position, double minDb = DefaultMinDb)
    {
        EnsureNegative(minDb);
        position = Math.Clamp(position, 0.0, 1.0);

        // Position 0 must be true silence, not just minDb, or muting via the
        // slider would leave a faint audible tail.
        if (position <= 0.0)
        {
            return 0f;
        }

        var db = minDb * (1.0 - position);
        return (float)Math.Clamp(DbToScalar(db), 0.0, 1.0);
    }

    /// <summary>WASAPI amplitude scalar (0..1) back to slider position (0..1).</summary>
    public static double ToPosition(double scalar, double minDb = DefaultMinDb)
    {
        EnsureNegative(minDb);
        scalar = Math.Clamp(scalar, 0.0, 1.0);

        if (scalar <= 0.0)
        {
            return 0.0;
        }

        var db = ScalarToDb(scalar);
        if (db <= minDb)
        {
            return 0.0;
        }

        return Math.Clamp(1.0 - (db / minDb), 0.0, 1.0);
    }

    public static double ScalarToDb(double scalar) =>
        scalar <= 0.0 ? double.NegativeInfinity : 20.0 * Math.Log10(scalar);

    public static double DbToScalar(double db) => Math.Pow(10.0, db / 20.0);

    private static void EnsureNegative(double minDb)
    {
        if (minDb >= 0.0 || double.IsNaN(minDb))
        {
            throw new ArgumentOutOfRangeException(
                nameof(minDb), minDb, "The floor must be a negative dB value.");
        }
    }
}
