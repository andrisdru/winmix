#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace winmix::audio {

// A process's user-facing identity, as far as we could determine it.
// friendlyName is the best available display name, never empty.
// executablePath is the full path to the image, or nullopt if unreadable.
struct ProcessInfo
{
    uint32_t pid = 0;
    std::wstring friendlyName;
    std::optional<std::wstring> executablePath;

    // Stamped by Resolve() from a monotonic counter. Unlike .NET object
    // identity (which the original test suite used to prove a cache hit vs.
    // a fresh resolution via Assert.Same/NotSame), a freed-then-reallocated
    // unordered_map node can land at the same address in native code, so
    // pointer identity alone cannot reliably distinguish the two here; this
    // field lets tests do it deterministically instead.
    uint64_t resolveSequence = 0;
};

// Resolves process ids to display names and executable paths, caching the
// result by reference so repeated Get() calls for a live pid return the same
// object.
//
// Resolution deliberately uses QueryFullProcessImageNameW against a handle
// opened with PROCESS_QUERY_LIMITED_INFORMATION rather than a broader access
// right: PROCESS_VM_READ-based approaches are denied for anything running at
// a higher elevation than us, so a plain user-level build would show blanks
// for a surprising number of real apps. PROCESS_QUERY_LIMITED_INFORMATION
// succeeds far more often.
class ProcessInfoCache
{
public:
    const ProcessInfo& Get(uint32_t pid);

    // Drops cache entries for processes that are no longer playing audio.
    // Windows recycles pids, so an unbounded cache would eventually mislabel
    // a session -- this must be called every refresh cycle, not just when
    // convenient.
    void Trim(const std::unordered_set<uint32_t>& livePids);

private:
    static ProcessInfo Resolve(uint32_t pid);
    static std::wstring DescribeImage(const std::wstring& path);
    static std::optional<std::wstring> TryGetProcessName(uint32_t pid);
    static std::optional<std::wstring> TryGetExecutablePath(uint32_t pid);

    std::unordered_map<uint32_t, ProcessInfo> cache_;
};

} // namespace winmix::audio
