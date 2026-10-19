#include "HudOverlay.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>

void HudOverlayApplyPlatformNoActivate(GLFWwindow* window, GLFWwindow* /*ownerWindow*/) {
    if (!window) return;
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow) return;

    [nsWindow setIgnoresMouseEvents:YES];
    [nsWindow setHidesOnDeactivate:NO];
    [nsWindow setExcludedFromWindowsMenu:YES];
    [nsWindow setLevel:NSFloatingWindowLevel];
    // Do not use Transient: those windows hide when the app is deactivated (Alt+Tab).
    [nsWindow setCollectionBehavior:
        NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorFullScreenAuxiliary |
        NSWindowCollectionBehaviorStationary |
        NSWindowCollectionBehaviorIgnoresCycle];
    [nsWindow orderFront:nil];
}

void HudOverlayKeepOnTop(GLFWwindow* window) {
    if (!window) return;
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow) return;
    [nsWindow setHidesOnDeactivate:NO];
    [nsWindow setLevel:NSFloatingWindowLevel];
}
