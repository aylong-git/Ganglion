#include "HudOverlay.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <shobjidl.h>

void HudOverlayApplyPlatformNoActivate(GLFWwindow* window, GLFWwindow* /*ownerWindow*/) {
    if (!window) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;

    // Do not set GWLP_HWNDPARENT. Owned windows hide/minimize with the owner
    // on Alt+Tab. These boards must stay visible over Chrome independently.

    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
    ex &= ~WS_EX_APPWINDOW;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    ITaskbarList* taskbar = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskbarList, reinterpret_cast<void**>(&taskbar))) && taskbar) {
        if (SUCCEEDED(taskbar->HrInit())) {
            taskbar->DeleteTab(hwnd);
        }
        taskbar->Release();
    }
}

void HudOverlayKeepOnTop(GLFWwindow* window) {
    if (!window) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
