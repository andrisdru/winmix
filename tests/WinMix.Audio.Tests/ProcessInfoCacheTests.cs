using System.Diagnostics;
using WinMix.Audio;
using Xunit;

namespace WinMix.Audio.Tests;

public class ProcessInfoCacheTests
{
    [Fact]
    public void PidZeroIsLabelledAsSystemSounds()
    {
        var cache = new ProcessInfoCache();

        var info = cache.Get(0);

        Assert.Equal("System sounds", info.FriendlyName);
        Assert.Null(info.ExecutablePath);
    }

    [Fact]
    public void ResolvesTheCurrentProcess()
    {
        var cache = new ProcessInfoCache();
        var pid = (uint)Environment.ProcessId;

        var info = cache.Get(pid);

        Assert.Equal(pid, info.Pid);
        Assert.NotNull(info.ExecutablePath);
        Assert.True(File.Exists(info.ExecutablePath));
        Assert.False(string.IsNullOrWhiteSpace(info.FriendlyName));
    }

    [Fact]
    public void RepeatedLookupsAreServedFromCache()
    {
        var cache = new ProcessInfoCache();
        var pid = (uint)Environment.ProcessId;

        Assert.Same(cache.Get(pid), cache.Get(pid));
    }

    [Fact]
    public void AnUnknownPidStillYieldsADisplayableName()
    {
        var cache = new ProcessInfoCache();

        // Odd values are never valid pids on Windows, so this cannot collide with
        // a live process and race the assertion.
        var info = cache.Get(uint.MaxValue - 1);

        Assert.False(string.IsNullOrWhiteSpace(info.FriendlyName));
        Assert.Null(info.ExecutablePath);
    }

    [Fact]
    public void TrimDropsProcessesThatStoppedPlaying()
    {
        var cache = new ProcessInfoCache();
        var pid = (uint)Environment.ProcessId;
        var first = cache.Get(pid);

        cache.Trim(new HashSet<uint>());

        // A fresh instance proves the entry was evicted rather than reused, which
        // is what keeps a recycled pid from inheriting the old label.
        Assert.NotSame(first, cache.Get(pid));
    }

    [Fact]
    public void TrimKeepsProcessesStillPlaying()
    {
        var cache = new ProcessInfoCache();
        var pid = (uint)Environment.ProcessId;
        var first = cache.Get(pid);

        cache.Trim(new HashSet<uint> { pid });

        Assert.Same(first, cache.Get(pid));
    }

    [Fact]
    public void FriendlyNameIsNeverAFullPath()
    {
        var cache = new ProcessInfoCache();

        var info = cache.Get((uint)Environment.ProcessId);

        Assert.DoesNotContain(Path.DirectorySeparatorChar, info.FriendlyName);
    }
}
