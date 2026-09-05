#include "winmix/audio/ProcessInfoCache.h"

#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cwctype>
#include <filesystem>
#include <vector>

namespace winmix::audio {

namespace {

constexpr uint32_t kProcessQueryLimitedInformation = 0x1000;

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

} // namespace

const ProcessInfo& ProcessInfoCache::Get(uint32_t pid)
{
    auto it = cache_.find(pid);
    if (it != cache_.end())
    {
        return it->second;
    }

    auto [inserted, ok] = cache_.emplace(pid, Resolve(pid));
    return inserted->second;
}

void ProcessInfoCache::Trim(const std::unordered_set<uint32_t>& livePids)
{
    if (cache_.empty())
    {
        return;
    }

    for (auto it = cache_.begin(); it != cache_.end();)
    {
        if (!livePids.contains(it->first))
        {
            it = cache_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

ProcessInfo ProcessInfoCache::Resolve(uint32_t pid)
{
    static std::atomic<uint64_t> sequenceCounter{0};
    const uint64_t sequence = sequenceCounter.fetch_add(1) + 1;

    if (pid == 0)
    {
        return ProcessInfo{pid, L"System sounds", std::nullopt, sequence};
    }

    auto path = TryGetExecutablePath(pid);
    if (!path)
    {
        auto name = TryGetProcessName(pid);
        std::wstring friendlyName = name ? *name : (L"PID " + std::to_wstring(pid));
        return ProcessInfo{pid, friendlyName, std::nullopt, sequence};
    }

    return ProcessInfo{pid, DescribeImage(*path), path, sequence};
}

// Prefers the executable's FileDescription ("Spotify") over its file name
// ("spotify"), matching what the Windows mixer shows.
std::wstring ProcessInfoCache::DescribeImage(const std::wstring& path)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size > 0)
    {
        std::vector<BYTE> buffer(size);
        if (GetFileVersionInfoW(path.c_str(), handle, size, buffer.data()))
        {
#pragma pack(push, 2)
            struct LangAndCodePage
            {
                WORD language;
                WORD codePage;
            };
#pragma pack(pop)

            LangAndCodePage* translations = nullptr;
            UINT translationsLen = 0;
            if (VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation",
                                reinterpret_cast<void**>(&translations), &translationsLen) &&
                translations != nullptr && translationsLen >= sizeof(LangAndCodePage))
            {
                wchar_t subBlock[64];
                swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileDescription",
                           translations[0].language, translations[0].codePage);

                LPWSTR description = nullptr;
                UINT descLen = 0;
                if (VerQueryValueW(buffer.data(), subBlock, reinterpret_cast<void**>(&description), &descLen) &&
                    description != nullptr && descLen > 0)
                {
                    const std::wstring trimmed = TrimW(description);
                    if (!trimmed.empty())
                    {
                        return trimmed;
                    }
                }
            }
        }
    }

    const std::wstring fileName = std::filesystem::path(path).stem().wstring();
    return fileName.empty() ? path : fileName;
}

std::optional<std::wstring> ProcessInfoCache::TryGetProcessName(uint32_t pid)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    std::optional<std::wstring> result;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == pid)
            {
                result = std::filesystem::path(entry.szExeFile).stem().wstring();
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

std::optional<std::wstring> ProcessInfoCache::TryGetExecutablePath(uint32_t pid)
{
    HANDLE handle = OpenProcess(kProcessQueryLimitedInformation, FALSE, pid);
    if (handle == nullptr)
    {
        return std::nullopt;
    }

    constexpr DWORD kCapacity = 1024;
    wchar_t buffer[kCapacity];
    DWORD size = kCapacity;

    // On success Windows rewrites size to the character count written.
    const bool ok = QueryFullProcessImageNameW(handle, 0, buffer, &size) != FALSE;
    CloseHandle(handle);

    if (!ok)
    {
        return std::nullopt;
    }

    return std::wstring(buffer, size);
}

} // namespace winmix::audio
