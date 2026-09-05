#include "MainWindow.h"

#include <windows.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // AudioSessionService's constructor calls CoCreateInstance, so COM must
    // already be initialized (STA) on this thread -- see its class comment
    // on the single-thread/apartment contract.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
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
    return exitCode;
}
