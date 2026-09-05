#include "winmix/audio/AppSessionTracker.h"

#include <algorithm>
#include <cwctype>
#include <unordered_map>

namespace winmix::audio {
namespace {
std::wstring AppKey(const AudioSessionSnapshot& session)
{
    if (session.isSystemSounds)
    {
        return L"system";
    }
    if (session.executablePath && !session.executablePath->empty())
    {
        auto path = *session.executablePath;
        std::transform(path.begin(), path.end(), path.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return L"app:" + path;
    }
    // Never merge unrelated apps just because their display names match.
    return session.pid ? L"pid:" + std::to_wstring(session.pid) : L"session:" + session.instanceId;
}
}

std::vector<AudioSessionSnapshot> AppSessionTracker::Refresh(
    const std::vector<AudioSessionSnapshot>& sessions, Clock::time_point now)
{
    std::vector<AudioSessionSnapshot> groups;
    std::vector<int> representativeRanks;
    std::unordered_map<std::wstring, size_t> indices;
    for (const auto& session : sessions)
    {
        const auto key = AppKey(session);
        const int rank = (session.hasOutputSession ? 2 : 0) + (session.IsActive() ? 1 : 0);
        const auto [it, inserted] = indices.emplace(key, groups.size());
        if (inserted)
        {
            groups.push_back(session);
            representativeRanks.push_back(rank);
            groups.back().instanceId = key;
            groups.back().sessionInstanceIds.clear();
            groups.back().inputSessionInstanceIds.clear();
            groups.back().processIds.clear();
            groups.back().inputProcessIds.clear();
            groups.back().activeOutputDeviceIds.clear();
            groups.back().activeInputDeviceIds.clear();
        }
        auto& group = groups[it->second];
        // Old inactive streams often coexist with the replacement stream.
        // They must not overwrite the volume/mute shown for live playback.
        if (rank > representativeRanks[it->second])
        {
            group.volume = session.volume;
            group.isMuted = session.isMuted;
            group.pid = session.pid;
            representativeRanks[it->second] = rank;
        }
        else if (rank == representativeRanks[it->second])
        {
            group.volume = std::max(group.volume, session.volume);
            group.isMuted = group.isMuted && session.isMuted;
            group.pid = std::min(group.pid, session.pid);
        }
        if (session.hasOutputSession && !group.hasOutputSession) group.peakLevel = session.peakLevel;
        else if (session.hasOutputSession == group.hasOutputSession) group.peakLevel = std::max(group.peakLevel, session.peakLevel);
        group.hasOutputSession = group.hasOutputSession || session.hasOutputSession;
        if (session.IsActive()) group.state = SessionState::Active;
        if (session.hasOutputSession) group.sessionInstanceIds.push_back(session.instanceId);
        else
        {
            group.inputSessionInstanceIds.push_back(session.instanceId);
            if (std::find(group.inputProcessIds.begin(), group.inputProcessIds.end(), session.pid) == group.inputProcessIds.end())
                group.inputProcessIds.push_back(session.pid);
        }
        for (const auto& deviceId : session.activeInputDeviceIds)
        {
            if (std::find(group.activeInputDeviceIds.begin(), group.activeInputDeviceIds.end(), deviceId) == group.activeInputDeviceIds.end())
                group.activeInputDeviceIds.push_back(deviceId);
        }
        for (const auto& deviceId : session.activeOutputDeviceIds)
        {
            if (std::find(group.activeOutputDeviceIds.begin(), group.activeOutputDeviceIds.end(), deviceId) == group.activeOutputDeviceIds.end())
                group.activeOutputDeviceIds.push_back(deviceId);
        }
        if (std::find(group.processIds.begin(), group.processIds.end(), session.pid) == group.processIds.end())
        {
            group.processIds.push_back(session.pid);
        }
    }

    for (auto& entry : entries_)
    {
        const auto it = indices.find(entry.snapshot.instanceId);
        if (it != indices.end())
        {
            entry.snapshot = std::move(groups[it->second]);
            entry.lastSeen = now;
            indices.erase(it);
        }
        else
        {
            entry.snapshot.sessionInstanceIds.clear();
            entry.snapshot.inputSessionInstanceIds.clear();
            entry.snapshot.activeInputDeviceIds.clear();
            entry.snapshot.activeOutputDeviceIds.clear();
            entry.snapshot.peakLevel = 0.0f;
            entry.snapshot.state = SessionState::Inactive;
        }
    }
    std::erase_if(entries_, [&](const Entry& entry) {
        return now - entry.lastSeen >= std::chrono::seconds(3);
    });
    // Preserve discovery order rather than the hash table's iteration order.
    for (auto& group : groups)
    {
        if (indices.contains(group.instanceId))
        {
            entries_.push_back({std::move(group), now});
        }
    }

    std::vector<AudioSessionSnapshot> result;
    for (const auto& entry : entries_)
    {
        result.push_back(entry.snapshot);
    }
    return result;
}
} // namespace winmix::audio
