#pragma once

#include <optional>
#include <string>

namespace winmix::audio {

// Turns the display name an audio session reports about itself into
// something worth showing a user.
//
// Most applications leave the field empty and are better identified by
// their executable, but Windows' own sessions populate it with an indirect
// resource reference such as @%SystemRoot%\System32\AudioSrv.Dll,-202. Left
// unresolved those render as raw gibberish.
class SessionNaming
{
public:
    // Returns a display-worthy session name, or nullopt when the session
    // offers nothing useful and the caller should fall back to the process
    // identity.
    static std::optional<std::wstring> Resolve(const std::optional<std::wstring>& reportedName);
};

} // namespace winmix::audio
