#include "HudOverlay.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>

#pragma comment(lib, "dwmapi.lib")

namespace {
// GLFW enables DWM per-pixel alpha when the window is created. The extended-style
// change below uses SWP_FRAMECHANGED, which rebuilds the frame and drops that.
// Without it, a cleared pixel (alpha 0) is composited as opaque black, so every
// icon's transparent background shows a black bar under the flicker square.
void EnablePerPixelAlpha(HWND hwnd) {
    DWM_BLURBEHIND bb = {};
    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.fEnable = TRUE;
    bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
    DwmEnableBlurBehindWindow(hwnd, &bb);
    DeleteObject(bb.hRgnBlur);
}
}

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

    EnablePerPixelAlpha(hwnd);
}

void HudOverlaySetVisibleRegion(GLFWwindow* window, int boardIndex, int square,
    int iconX, int iconY, int iconW, int iconH,
    const unsigned char* opaque, int srcW, int srcH) {
    if (!window || boardIndex < 0 || boardIndex > 3) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd || square <= 0) return;

    struct Key {
        GLFWwindow* window = nullptr;
        int square = -1;
        int x = 0, y = 0, w = 0, h = 0;
        const unsigned char* opaque = nullptr;
        int srcW = 0, srcH = 0;
    };
    static Key cache[4];
    Key& prev = cache[boardIndex];
    if (prev.window == window && prev.square == square
        && prev.x == iconX && prev.y == iconY && prev.w == iconW && prev.h == iconH
        && prev.opaque == opaque && prev.srcW == srcW && prev.srcH == srcH) {
        return;
    }
    prev = { window, square, iconX, iconY, iconW, iconH, opaque, srcW, srcH };

    HRGN shape = CreateRectRgn(0, 0, square, square);
    const bool haveIcon = opaque && srcW > 0 && srcH > 0 && iconW > 0 && iconH > 0;
    if (haveIcon) {
        for (int py = 0; py < iconH; ++py) {
            int sy = (py * srcH) / iconH;
            if (sy >= srcH) sy = srcH - 1;
            const unsigned char* row = opaque + static_cast<size_t>(sy) * static_cast<size_t>(srcW);
            int x = 0;
            while (x < iconW) {
                int sx = (x * srcW) / iconW;
                if (sx >= srcW || !row[sx]) {
                    ++x;
                    continue;
                }
                const int x0 = x;
                while (x < iconW) {
                    int sx2 = (x * srcW) / iconW;
                    if (sx2 >= srcW || !row[sx2]) break;
                    ++x;
                }
                HRGN run = CreateRectRgn(iconX + x0, iconY + py, iconX + x, iconY + py + 1);
                CombineRgn(shape, shape, run, RGN_OR);
                DeleteObject(run);
            }
        }
    }
    // SetWindowRgn takes ownership of shape.
    SetWindowRgn(hwnd, shape, TRUE);
}

void HudOverlayKeepOnTop(GLFWwindow* window) {
    if (!window) return;
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    EnablePerPixelAlpha(hwnd);
}
