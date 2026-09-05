#include "winmix/audio/AudioSessionService.h"
#include "winmix/audio/AppOutputRouter.h"
#include "winmix/audio/AppDeviceRouter.h"
#include "winmix/audio/AudioPolicyConfigFactory.h"
#include <wrl/wrappers/corewrappers.h>
#include <array>
#include <audioclient.h>

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
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace {
void TestCaptureDiscovery(winmix::audio::AudioSessionService& service, bool hold)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client)))
        throw std::runtime_error("Could not create the capture discovery fixture.");
    WAVEFORMATEX* format = nullptr;
    if (FAILED(client->GetMixFormat(&format))) throw std::runtime_error("Could not get microphone format.");
    const auto hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, format, nullptr);
    CoTaskMemFree(format);
    if (FAILED(hr)) throw std::runtime_error("Could not initialize the capture discovery fixture.");
    // Do not Start() or read audio. An initialized, inactive session is
    // sufficient to verify discovery and provide a card for UI testing.
    const auto apps = service.Refresh();
    const auto app = std::find_if(apps.begin(), apps.end(), [](const auto& s) { return s.pid == GetCurrentProcessId(); });
    if (app == apps.end() || app->inputSessionInstanceIds.empty() || !app->sessionInstanceIds.empty() || app->hasOutputSession)
        throw std::runtime_error("Recording-only app discovery failed.");
    std::wprintf(L"Capture-only discovery passed (pid=%u); no audio was recorded.\n", GetCurrentProcessId());
    if (hold) std::this_thread::sleep_for(std::chrono::seconds(30));
}

void TestInputRouting(const std::vector<winmix::audio::AudioDeviceInfo>& devices)
{
    using winmix::audio::AppDeviceRouter;
    using winmix::audio::AudioPolicyConfigFactory;
    const uint32_t pid = GetCurrentProcessId();
    const std::array<ERole, 3> roles{eConsole, eMultimedia, eCommunications};
    std::array<Microsoft::WRL::Wrappers::HString, 3> original;
    std::array<std::optional<std::wstring>, 3> outputBefore;
    for (size_t i = 0; i < roles.size(); ++i)
    {
        if (FAILED(AudioPolicyConfigFactory::GetPersistedDefaultAudioEndpoint(pid, eCapture, roles[i], original[i].GetAddressOf())) ||
            FAILED(AppDeviceRouter::Get(pid, eRender, outputBefore[i], roles[i])))
            throw std::runtime_error("Could not save smoke-test routing preferences.");
    }
    auto restore = [&]() {
        HRESULT failure = S_OK;
        for (size_t i = 0; i < roles.size(); ++i)
        {
            const auto hr = AudioPolicyConfigFactory::SetPersistedDefaultAudioEndpoint(pid, eCapture, roles[i], original[i].Get());
            if (FAILED(hr)) failure = hr;
        }
        if (FAILED(failure)) throw std::runtime_error("Could not restore smoke-test input preferences.");
    };
    try
    {
        for (const auto& device : devices)
        {
            if (FAILED(AppDeviceRouter::Set(pid, eCapture, device.id)))
                throw std::runtime_error("Setting the test microphone failed.");
            for (const auto role : roles)
            {
                std::optional<std::wstring> actual;
                if (FAILED(AppDeviceRouter::Get(pid, eCapture, actual, role)) || actual != device.id)
                    throw std::runtime_error("Input preference readback did not match.");
            }
            std::wprintf(L"Input route test: %s -- all three roles passed\n", device.friendlyName.c_str());
        }
        if (FAILED(AppDeviceRouter::Set(pid, eCapture, std::nullopt)))
            throw std::runtime_error("Clearing the microphone preference failed.");
        for (size_t i = 0; i < roles.size(); ++i)
        {
            std::optional<std::wstring> input, output;
            if (FAILED(AppDeviceRouter::Get(pid, eCapture, input, roles[i])) || input ||
                FAILED(AppDeviceRouter::Get(pid, eRender, output, roles[i])) || output != outputBefore[i])
                throw std::runtime_error("Input reset or output isolation check failed.");
        }
    }
    catch (...)
    {
        restore();
        throw;
    }
    restore();
    std::wprintf(L"Input reset passed; output preferences unchanged; original input preferences restored.\n");
}

void TestAppRouting(winmix::audio::AudioSessionService& service, uint32_t pid, const std::wstring& target)
{
    const auto apps = service.Refresh();
    const auto it = std::find_if(apps.begin(), apps.end(), [&](const auto& app) {
        return std::find(app.processIds.begin(), app.processIds.end(), pid) != app.processIds.end();
    });
    if (it == apps.end() || !it->outputDeviceKnown || !it->IsActive())
        throw std::runtime_error("Choose a playing app with a readable routing preference.");
    const auto original = *it;
    if (original.activeOutputDeviceIds.size() == 1 && original.activeOutputDeviceIds[0] == target)
        throw std::runtime_error("Choose an output different from the app's current active output.");
    auto restore = [&]() {
        service.SetAppOutputDevice(original.instanceId, original.outputDeviceId);
        for (int i = 0; i < 10; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            service.Refresh();
        }
        service.SetVolume(original.instanceId, original.volume);
        service.SetMute(original.instanceId, original.isMuted);
    };
    try
    {
        service.SetAppOutputDevice(original.instanceId, target);
        bool moved = false;
        for (int i = 0; i < 30; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto current = service.Refresh();
            const auto app = std::find_if(current.begin(), current.end(), [&](const auto& s) { return s.instanceId == original.instanceId; });
            if (app == current.end()) throw std::runtime_error("App identity disappeared during switching.");
            if (app->activeOutputDeviceIds.size() == 1 && app->activeOutputDeviceIds[0] == target)
            {
                std::wprintf(L"%s: active stream moved; stable app ID; volume before=%.3f after=%.3f\n",
                    app->displayName.c_str(), original.volume, app->volume);
                if (std::abs(app->volume - original.volume) > 0.01f || app->isMuted != original.isMuted)
                    throw std::runtime_error("Switching did not preserve app volume and mute.");
                moved = true;
                break;
            }
        }
        if (!moved) throw std::runtime_error("The active stream did not move to the requested output within three seconds.");
    }
    catch (...)
    {
        restore();
        throw;
    }
    restore();
    std::wprintf(L"Original app output preference, volume, and mute restored.\n");
}

void TestRouting(const std::vector<winmix::audio::AudioDeviceInfo>& devices)
{
    using winmix::audio::AppOutputRouter;
    const uint32_t pid = GetCurrentProcessId();
    std::optional<std::wstring> original;
    if (FAILED(AppOutputRouter::Get(pid, original)))
        throw std::runtime_error("Could not read the smoke-test process's routing policy.");
    try
    {
        for (const auto& device : devices)
        {
            const HRESULT hr = AppOutputRouter::Set(pid, device.id);
            std::wprintf(L"Route test: %s HRESULT=0x%08lX\n", device.friendlyName.c_str(), static_cast<unsigned long>(hr));
            std::optional<std::wstring> actual;
            if (FAILED(hr) || FAILED(AppOutputRouter::Get(pid, actual)) || actual != device.id)
                throw std::runtime_error("Routing readback did not match the requested endpoint.");
        }
        std::optional<std::wstring> actual;
        if (FAILED(AppOutputRouter::Set(pid, std::nullopt)) ||
            FAILED(AppOutputRouter::Get(pid, actual)) || actual)
            throw std::runtime_error("Clearing the app output preference failed.");
    }
    catch (...)
    {
        AppOutputRouter::Set(pid, original);
        throw;
    }
    if (FAILED(AppOutputRouter::Set(pid, original)))
        throw std::runtime_error("Could not restore the smoke-test process's routing policy.");
    std::wprintf(L"Routing readback and reset passed; original preference restored.\n");
}
}

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
        const bool once = argc > 1 && std::string(argv[1]) == "--once";
        const bool testRouting = argc > 1 && std::string(argv[1]) == "--test-routing";
        const bool testInputRouting = argc > 1 && std::string(argv[1]) == "--test-input-routing";
        const bool testCapture = argc > 1 && std::string(argv[1]) == "--test-capture-discovery";
        const bool holdCapture = argc > 1 && std::string(argv[1]) == "--capture-fixture";
        const bool testAppRouting = argc == 4 && std::string(argv[1]) == "--test-app-routing";

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
        }

        if (testRouting) TestRouting(outputDevices);
        if (testInputRouting) TestInputRouting(service.ListInputDevices());
        if (testCapture || holdCapture) TestCaptureDiscovery(service, holdCapture);
        if (testAppRouting)
        {
            const auto index = static_cast<size_t>(std::stoul(argv[3]));
            if (index >= outputDevices.size()) throw std::runtime_error("Invalid output device index.");
            TestAppRouting(service, static_cast<uint32_t>(std::stoul(argv[2])), outputDevices[index].id);
        }

        std::wprintf(L"Polling audio sessions. Press Ctrl+C to stop.\n");

        while (!(argc >= 3 && std::string(argv[1]) == "--set-default"))
        {
            const auto snapshots = service.Refresh();

            std::wprintf(L"--- %zu session(s) ---\n", snapshots.size());
            for (const auto& s : snapshots)
            {
                std::wprintf(
                    L"  pid=%-6u active=%-5s muted=%-5s vol=%.2f peak=%.2f id=%s  %s\n",
                    s.pid,
                    s.IsActive() ? L"true" : L"false",
                    s.isMuted ? L"true" : L"false",
                    s.volume,
                    s.peakLevel,
                    s.instanceId.c_str(),
                    s.displayName.c_str());
                std::wprintf(L"    streams=%zu output=%s\n", s.sessionInstanceIds.size(),
                    s.outputDeviceKnown ? (s.outputDeviceId ? s.outputDeviceId->c_str() : L"Default") : L"Unknown");
                for (const auto& id : s.activeOutputDeviceIds) std::wprintf(L"    playing-on=%s\n", id.c_str());
                std::wprintf(L"    input-streams=%zu input=%s\n", s.inputSessionInstanceIds.size(),
                    s.inputDeviceKnown ? (s.inputDeviceId ? s.inputDeviceId->c_str() : L"Default") : L"Unknown");
                for (const auto& id : s.activeInputDeviceIds) std::wprintf(L"    recording-on=%s\n", id.c_str());
            }

            if (once || testRouting || testInputRouting || testAppRouting || testCapture || holdCapture) break;

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
