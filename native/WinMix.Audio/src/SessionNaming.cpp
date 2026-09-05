#include "winmix/audio/SessionNaming.h"

#include <windows.h>
#include <shlwapi.h>

namespace winmix::audio {

namespace {

std::wstring TrimW(const std::wstring& s)
{
    const auto begin = s.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos)
    {
        return L"";
    }
    const auto end = s.find_last_not_of(L" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Returns a failure HRESULT when the module or resource id cannot be found,
// which happens routinely for third-party sessions.
std::optional<std::wstring> LoadIndirectString(const std::wstring& source)
{
    constexpr int kCapacity = 512;
    wchar_t buffer[kCapacity];

    const HRESULT hr = SHLoadIndirectString(source.c_str(), buffer, kCapacity, nullptr);
    if (FAILED(hr))
    {
        return std::nullopt;
    }

    return std::wstring(buffer);
}

} // namespace

std::optional<std::wstring> SessionNaming::Resolve(const std::optional<std::wstring>& reportedName)
{
    if (!reportedName)
    {
        return std::nullopt;
    }

    const std::wstring trimmed = TrimW(*reportedName);
    if (trimmed.empty())
    {
        return std::nullopt;
    }

    // Only strings beginning with '@' are indirect references; everything
    // else is already literal text the app chose for itself.
    if (trimmed.front() != L'@')
    {
        return trimmed;
    }

    const auto expanded = LoadIndirectString(trimmed);
    if (!expanded)
    {
        return std::nullopt;
    }

    const std::wstring result = TrimW(*expanded);
    return result.empty() ? std::nullopt : std::make_optional(result);
}

} // namespace winmix::audio
