#include "doctest.h"
#include "winmix/audio/AppSessionTracker.h"

using namespace winmix::audio;
using namespace std::chrono_literals;

namespace {
AudioSessionSnapshot Session(std::wstring id, uint32_t pid, std::wstring path,
                             SessionState state = SessionState::Active)
{
    AudioSessionSnapshot s;
    s.instanceId = std::move(id);
    s.pid = pid;
    s.executablePath = std::move(path);
    s.displayName = L"Player";
    s.volume = 0.4f;
    s.peakLevel = 0.3f;
    s.state = state;
    return s;
}
const AppSessionTracker::Clock::time_point start{};
}

TEST_CASE("Device switch keeps one app through overlapping and missing sessions")
{
    AppSessionTracker tracker;
    auto old = Session(L"speakers:1", 100, L"C:\\Apps\\Spotify.exe");
    const auto initial = tracker.Refresh({old}, start);
    REQUIRE(initial.size() == 1);
    const auto key = initial[0].instanceId;
    old.state = SessionState::Inactive;
    old.volume = 0.9f;
    old.isMuted = true;
    auto replacement = Session(L"headphones:2", 200, L"c:\\apps\\spotify.exe");
    replacement.activeOutputDeviceIds = {L"headphones"};
    auto overlapping = tracker.Refresh({old, replacement}, start + 100ms);
    REQUIRE(overlapping.size() == 1);
    CHECK(overlapping[0].instanceId == key);
    CHECK(overlapping[0].sessionInstanceIds.size() == 2);
    CHECK(overlapping[0].processIds.size() == 2);
    CHECK(overlapping[0].volume == doctest::Approx(0.4f));
    CHECK_FALSE(overlapping[0].isMuted);
    CHECK(overlapping[0].pid == 200);
    REQUIRE(overlapping[0].activeOutputDeviceIds.size() == 1);
    CHECK(overlapping[0].activeOutputDeviceIds[0] == L"headphones");

    auto gap = tracker.Refresh({}, start + 200ms);
    REQUIRE(gap.size() == 1);
    CHECK(gap[0].instanceId == key);
    CHECK(gap[0].sessionInstanceIds.empty());
    CHECK(gap[0].activeOutputDeviceIds.empty());
    CHECK(gap[0].peakLevel == 0);
    CHECK_FALSE(gap[0].IsActive());

    auto resumed = tracker.Refresh({replacement}, start + 500ms);
    REQUIRE(resumed.size() == 1);
    CHECK(resumed[0].instanceId == key);
    REQUIRE(resumed[0].sessionInstanceIds.size() == 1);
    CHECK(resumed[0].sessionInstanceIds[0] == L"headphones:2");
    CHECK(tracker.Refresh({}, start + 3499ms).size() == 1);
    CHECK(tracker.Refresh({}, start + 3500ms).empty());
}

TEST_CASE("App order is stable when enumeration order and worker PIDs change")
{
    AppSessionTracker tracker;
    auto chrome = Session(L"c1", 10, L"C:\\Chrome.exe");
    auto spotify = Session(L"s1", 20, L"C:\\Spotify.exe");
    const auto initial = tracker.Refresh({chrome, spotify}, start);
    spotify.instanceId = L"s2";
    chrome.pid = 11;
    chrome.instanceId = L"c2";
    const auto next = tracker.Refresh({spotify, chrome}, start + 100ms);
    REQUIRE(next.size() == 2);
    CHECK(next[0].instanceId == initial[0].instanceId);
    CHECK(next[1].instanceId == initial[1].instanceId);
    CHECK(next[0].pid == 11);
}

TEST_CASE("Multiple Chrome streams share one control with all session IDs")
{
    AppSessionTracker tracker;
    auto first = Session(L"c1", 10, L"C:\\Chrome.exe");
    auto second = Session(L"c2", 10, L"C:\\Chrome.exe");
    second.peakLevel = 0.8f;
    auto third = Session(L"c3", 11, L"C:\\Chrome.exe");
    const auto apps = tracker.Refresh({first, second, third}, start);
    REQUIRE(apps.size() == 1);
    CHECK(apps[0].sessionInstanceIds.size() == 3);
    CHECK(apps[0].processIds.size() == 2);
    CHECK(apps[0].peakLevel == doctest::Approx(0.8f));
}

TEST_CASE("Matching display names do not merge unrelated executables or unknown processes")
{
    AppSessionTracker tracker;
    auto first = Session(L"a", 10, L"C:\\One\\Player.exe");
    auto second = Session(L"b", 11, L"C:\\Two\\Player.exe");
    auto unknown = Session(L"c", 12, L"");
    unknown.executablePath.reset();
    auto otherUnknown = unknown;
    otherUnknown.instanceId = L"d";
    otherUnknown.pid = 13;
    CHECK(tracker.Refresh({first, second, unknown, otherUnknown}, start).size() == 4);
}

TEST_CASE("Paused non-default apps and system sounds keep stable controls")
{
    AppSessionTracker tracker;
    auto paused = Session(L"remote:paused", 10, L"C:\\Player.exe", SessionState::Inactive);
    auto system = Session(L"system:speakers", 0, L"");
    system.isSystemSounds = true;
    auto otherSystem = system;
    otherSystem.instanceId = L"system:headphones";
    const auto apps = tracker.Refresh({paused, system, otherSystem}, start);
    REQUIRE(apps.size() == 2);
    CHECK_FALSE(apps[0].IsActive());
    CHECK(apps[1].isSystemSounds);
    CHECK(apps[1].sessionInstanceIds.size() == 2);
}

TEST_CASE("Recording-only apps keep their identity across microphone switches")
{
    AppSessionTracker tracker;
    auto mic = Session(L"mic:first", 77, L"C:\\Recorder.exe");
    mic.hasOutputSession = false;
    mic.volume = 0.0f;
    mic.activeInputDeviceIds = {L"mic-a"};
    auto first = tracker.Refresh({mic}, start);
    REQUIRE(first.size() == 1);
    CHECK_FALSE(first[0].hasOutputSession);
    CHECK(first[0].sessionInstanceIds.empty());
    CHECK(first[0].inputSessionInstanceIds == std::vector<std::wstring>{L"mic:first"});
    CHECK(first[0].inputProcessIds == std::vector<uint32_t>{77});
    const auto key = first[0].instanceId;
    auto gap = tracker.Refresh({}, start + 100ms);
    REQUIRE(gap.size() == 1);
    CHECK(gap[0].inputSessionInstanceIds.empty());
    CHECK(gap[0].activeInputDeviceIds.empty());
    mic.instanceId = L"mic:second";
    mic.activeInputDeviceIds = {L"mic-b"};
    mic.pid = 78;
    auto switched = tracker.Refresh({mic}, start + 200ms);
    REQUIRE(switched.size() == 1);
    CHECK(switched[0].instanceId == key);
    CHECK(switched[0].activeInputDeviceIds == std::vector<std::wstring>{L"mic-b"});
    CHECK(switched[0].inputProcessIds == std::vector<uint32_t>{78});
}

TEST_CASE("Microphone workers share the app card without changing playback controls")
{
    auto output = Session(L"out", 10, L"C:\\Browser.exe", SessionState::Inactive);
    output.volume = 0.25f;
    output.isMuted = true;
    output.peakLevel = 0.0f;
    auto input = Session(L"in", 11, L"C:\\Browser.exe");
    input.hasOutputSession = false;
    input.volume = 1.0f;
    input.peakLevel = 0.9f;
    input.activeInputDeviceIds = {L"mic"};
    for (const auto& sessions : {std::vector<AudioSessionSnapshot>{output, input},
                                std::vector<AudioSessionSnapshot>{input, output}})
    {
        AppSessionTracker tracker;
        const auto apps = tracker.Refresh(sessions, start);
        REQUIRE(apps.size() == 1);
        CHECK(apps[0].IsActive());
        CHECK(apps[0].hasOutputSession);
        CHECK(apps[0].volume == doctest::Approx(0.25f));
        CHECK(apps[0].isMuted);
        CHECK(apps[0].peakLevel == 0.0f);
        CHECK(apps[0].pid == 10);
        CHECK(apps[0].processIds.size() == 2);
        CHECK(apps[0].sessionInstanceIds == std::vector<std::wstring>{L"out"});
        CHECK(apps[0].inputSessionInstanceIds == std::vector<std::wstring>{L"in"});
        CHECK(apps[0].activeInputDeviceIds == std::vector<std::wstring>{L"mic"});
    }
}
