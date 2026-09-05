#include "MainWindow.h"

#include <windows.h>
#include <objbase.h>

namespace {

// Session-local (Local\ prefix), matching the .NET port's mutex name and
// scope exactly. There is deliberately no re-activation IPC: a second
// launch just quietly exits, same as today -- it does not forward an "open
// mixer" signal to the first instance.
constexpr wchar_t kInstanceMutexName[] = L"Local\\WinMixCpp.SingleInstance";

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
    const bool ownsInstance = instanceMutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
    if (!ownsInstance)
    {
        if (instanceMutex)
        {
            CloseHandle(instanceMutex);
        }
        return 0;
    }

    // AudioSessionService's constructor calls CoCreateInstance, so COM must
    // already be initialized (STA) on this thread -- see its class comment
    // on the single-thread/apartment contract.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
        return 1;
    }

    int exitCode;
    {
        // Scoped so MainWindow (and its AudioSessionService, whose ComPtr
        // members must Release() while COM is still up) is destroyed before
        // CoUninitialize() below runs.
        winmix::app::MainWindow window(hInstance);
        window.Show(nCmdShow);
        exitCode = window.RunMessageLoop();
    }

    CoUninitialize();

    ReleaseMutex(instanceMutex);
    CloseHandle(instanceMutex);
    return exitCode;
}
