#include "MainWindow.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    winmix::app::MainWindow window(hInstance);
    window.Show(nCmdShow);
    return window.RunMessageLoop();
}
