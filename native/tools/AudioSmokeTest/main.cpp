#include "winmix/audio/AudioSessionService.h"

#include <windows.h>
#include <objbase.h>
#include <fcntl.h>
#include <io.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>

int main()
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    setvbuf(stdout, nullptr, _IONBF, 0);

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        std::fwprintf(stderr, L"CoInitializeEx failed: 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    int exitCode = 0;
    try
    {
        winmix::audio::AudioSessionService service;

        std::wprintf(L"Polling audio sessions. Press Ctrl+C to stop.\n");

        while (true)
        {
            const auto snapshots = service.Refresh();

            std::wprintf(L"--- %zu session(s) ---\n", snapshots.size());
            for (const auto& s : snapshots)
            {
                std::wprintf(
                    L"  pid=%-6u active=%-5s muted=%-5s vol=%.2f peak=%.2f  %s\n",
                    s.pid,
                    s.IsActive() ? L"true" : L"false",
                    s.isMuted ? L"true" : L"false",
                    s.volume,
                    s.peakLevel,
                    s.displayName.c_str());
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "Fatal error: %s\n", ex.what());
        exitCode = 1;
    }

    CoUninitialize();
    return exitCode;
}
