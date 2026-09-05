#include "MainWindow.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

namespace {

// Session-local (Local\ prefix) so it can't collide with another user's
// session on the same machine. There is deliberately no re-activation IPC:
// a second launch just quietly exits -- it does not forward an "open mixer"
// signal to the first instance.
constexpr wchar_t kInstanceMutexName[] = L"Local\\WinMix.SingleInstance";

// Appended to the Run-key command line by Autostart::SetEnabled() -- lets
// WinMain tell "the user double-clicked WinMix.exe" apart from "Windows
// launched it at sign-in", which nCmdShow alone can't do (the shell passes
// the same show command either way).
constexpr wchar_t kMinimizedArg[] = L"--minimized";

bool StartedMinimized()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        return false;
    }

    bool found = false;
    for (int i = 1; i < argc; ++i)
    {
        if (lstrcmpiW(argv[i], kMinimizedArg) == 0)
        {
            found = true;
            break;
        }
    }

    LocalFree(argv);
    return found;
}

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
        window.Show(nCmdShow, StartedMinimized());
        exitCode = window.RunMessageLoop();
    }

    CoUninitialize();

    ReleaseMutex(instanceMutex);
    CloseHandle(instanceMutex);
    return exitCode;
}
