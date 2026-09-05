#include "doctest.h"
#include "winmix/audio/ProcessInfoCache.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>

using winmix::audio::ProcessInfoCache;

TEST_CASE("PidZeroIsLabelledAsSystemSounds")
{
    ProcessInfoCache cache;

    const auto& info = cache.Get(0);

    CHECK(info.friendlyName == L"System sounds");
    CHECK(!info.executablePath.has_value());
}

TEST_CASE("ResolvesTheCurrentProcess")
{
    ProcessInfoCache cache;
    const uint32_t pid = GetCurrentProcessId();

    const auto& info = cache.Get(pid);

    CHECK(info.pid == pid);
    REQUIRE(info.executablePath.has_value());
    CHECK(std::filesystem::exists(*info.executablePath));
    CHECK(!info.friendlyName.empty());
}

TEST_CASE("RepeatedLookupsAreServedFromCache")
{
    ProcessInfoCache cache;
    const uint32_t pid = GetCurrentProcessId();

    const uint64_t first = cache.Get(pid).resolveSequence;
    const uint64_t second = cache.Get(pid).resolveSequence;

    CHECK(first == second);
}

TEST_CASE("AnUnknownPidStillYieldsADisplayableName")
{
    ProcessInfoCache cache;

    // Not a multiple of 4, so this can never collide with a live Windows pid.
    const auto& info = cache.Get(UINT32_MAX - 1);

    CHECK(!info.friendlyName.empty());
    CHECK(!info.executablePath.has_value());
}

TEST_CASE("TrimDropsProcessesThatStoppedPlaying")
{
    ProcessInfoCache cache;
    const uint32_t pid = GetCurrentProcessId();
    const uint64_t first = cache.Get(pid).resolveSequence;

    cache.Trim({});

    // A different sequence number proves the entry was evicted and
    // re-resolved rather than reused, which is what keeps a recycled pid
    // from inheriting the old label.
    CHECK(cache.Get(pid).resolveSequence != first);
}

TEST_CASE("TrimKeepsProcessesStillPlaying")
{
    ProcessInfoCache cache;
    const uint32_t pid = GetCurrentProcessId();
    const uint64_t first = cache.Get(pid).resolveSequence;

    cache.Trim({pid});

    CHECK(cache.Get(pid).resolveSequence == first);
}

TEST_CASE("FriendlyNameIsNeverAFullPath")
{
    ProcessInfoCache cache;

    const auto& info = cache.Get(GetCurrentProcessId());

    CHECK(info.friendlyName.find(L'\\') == std::wstring::npos);
}
