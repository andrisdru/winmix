#pragma once

#include "winmix/audio/AudioSessionSnapshot.h"
#include <chrono>
#include <vector>

namespace winmix::audio {

// One mixer control per executable, independent of endpoint and audio worker
// PID. Retains a missing app briefly while Windows recreates its streams.
class AppSessionTracker
{
public:
    using Clock = std::chrono::steady_clock;
    std::vector<AudioSessionSnapshot> Refresh(
        const std::vector<AudioSessionSnapshot>& sessions, Clock::time_point now);

private:
    struct Entry
    {
        AudioSessionSnapshot snapshot;
        Clock::time_point lastSeen;
    };
    std::vector<Entry> entries_;
};

} // namespace winmix::audio
