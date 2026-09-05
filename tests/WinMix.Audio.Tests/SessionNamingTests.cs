using WinMix.Audio;
using Xunit;

namespace WinMix.Audio.Tests;

public class SessionNamingTests
{
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("\t\r\n")]
    public void SessionsWithoutANameYieldNull(string? reported)
    {
        // Null tells the caller to fall back to the process identity, which is the
        // common path: most applications never populate this field.
        Assert.Null(SessionNaming.Resolve(reported));
    }

    [Theory]
    [InlineData("Spotify", "Spotify")]
    [InlineData("  Firefox  ", "Firefox")]
    [InlineData("Game Audio", "Game Audio")]
    public void LiteralNamesPassThroughTrimmed(string reported, string expected)
    {
        Assert.Equal(expected, SessionNaming.Resolve(reported));
    }

    [Fact]
    public void AnUnresolvableIndirectReferenceYieldsNull()
    {
        // Rather than surfacing the raw "@..." string to the user, an unresolvable
        // reference has to degrade to the process name.
        var result = SessionNaming.Resolve(@"@%SystemRoot%\System32\definitely-not-real.dll,-999");

        Assert.Null(result);
    }

    [Fact]
    public void AMalformedIndirectReferenceDoesNotThrow()
    {
        Assert.Null(SessionNaming.Resolve("@"));
        Assert.Null(SessionNaming.Resolve("@garbage"));
    }

    [Fact]
    public void ANameContainingAnAtSignIsNotTreatedAsIndirect()
    {
        // Only a leading '@' marks an indirect reference; an address-like name is
        // literal text the application chose.
        Assert.Equal("user@example.com", SessionNaming.Resolve("user@example.com"));
    }

    [Fact]
    public void ResolvedNamesNeverKeepTheIndirectionMarker()
    {
        // Whatever an indirect reference resolves to, the user must never see the
        // "@module,-id" form leak through.
        var result = SessionNaming.Resolve(@"@%SystemRoot%\System32\AudioSrv.Dll,-202");

        Assert.True(result is null || !result.StartsWith('@'), $"Leaked raw reference: {result}");
    }
}
