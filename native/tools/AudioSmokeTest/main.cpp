#include "winmix/audio/AudioSessionService.h"

#include <windows.h>
#include <objbase.h>
#include <fcntl.h>
#include <io.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>

int main(int argc, char** argv)
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

        const auto outputDevices = service.ListOutputDevices();
        std::wprintf(L"--- output devices ---\n");
        for (const auto& d : outputDevices)
        {
            std::wprintf(L"  [%s] id=%s  %s\n", d.isDefault ? L"default" : L"       ", d.id.c_str(), d.friendlyName.c_str());
        }
        std::wprintf(L"--- input devices ---\n");
        for (const auto& d : service.ListInputDevices())
        {
            std::wprintf(L"  [%s] id=%s  %s\n", d.isDefault ? L"default" : L"       ", d.id.c_str(), d.friendlyName.c_str());
        }

        // Diagnostic: `AudioSmokeTest --set-default <index>` isolates
        // DefaultEndpointSwitcher/PolicyConfigInterop from any UI/reentrancy
        // complexity, to test it standalone.
        if (argc >= 3 && std::string(argv[1]) == "--set-default")
        {
            const size_t index = static_cast<size_t>(std::atoi(argv[2]));
            if (index < outputDevices.size())
            {
                std::wprintf(L"Setting default output device to: %s\n", outputDevices[index].friendlyName.c_str());
                service.SetDefaultOutputDevice(outputDevices[index].id);
                std::wprintf(L"SetDefaultOutputDevice returned normally.\n");
            }
            CoUninitialize();
            return 0;
        }

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
