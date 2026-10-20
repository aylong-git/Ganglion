#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include <glad/glad.h>
#include <setupapi.h>
#include <devguid.h>
#include "UI.h"
#include "implot.h"
#include "Utils.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "InputManager.h"
#include "KeyIconCatalog.h"
#include "SSVEPTargets.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <random>
#include <sstream>
#include <iomanip>
#include <string>

#pragma comment(lib, "setupapi.lib")

void UIManager::UpdateAvailablePorts() {
    availablePorts.clear();

#if defined(_WIN32) || defined(_WIN64)
    // ==================================================
    // --- WINDOWS HARDWARE SETUPAPI FILTERING ---
    // ==================================================
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (deviceInfoSet != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA deviceInfoData;
        deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
            char friendlyName[256] = { 0 };
            DWORD dataType;

            // Retrieve the Device Manager "Friendly Name" string
            if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData, SPDRP_FRIENDLYNAME,
                &dataType, (PBYTE)friendlyName, sizeof(friendlyName), NULL)) {
                std::string nameStr(friendlyName);

                // Filter out non-Ganglion devices safely
                if (nameStr.find("Serial") != std::string::npos) {
                    size_t start = nameStr.find("(COM");
                    size_t end = nameStr.find(")", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        std::string extractedPort = nameStr.substr(start + 1, end - start - 1);
                        availablePorts.push_back(extractedPort);
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }

#elif defined(__APPLE__)
    // ==================================================
    // --- MACOS IOKIT HARDWARE FILTERING ---
    // ==================================================
    // Note: Requires linking: -framework IOKit -framework CoreFoundation
    io_iterator_t iter;
    CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOSerialBSDServiceValue);
    if (matchingDict) {
        // Look through all serial devices (including USB Modems)
        CFDictionarySetValue(matchingDict, CFSTR(kIOSerialBSDTypeKey), CFSTR(kIOSerialBSDAllTypes));
        if (IOServiceGetMatchingServices(0, matchingDict, &iter) == KERN_SUCCESS) {
            io_object_t device;
            while ((device = IOIteratorNext(iter))) {
                // Fetch the actual dialout callout path (e.g., /dev/cu.usbmodem14101)
                CFTypeRef pathRef = IORegistryEntryCreateCFProperty(device, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
                if (pathRef) {
                    char pathBuf[256];
                    if (CFStringGetCString((CFStringRef)pathRef, pathBuf, sizeof(pathBuf), kCFStringEncodingUTF8)) {
                        std::string pathStr(pathBuf);
                        bool isDongle = false;

                        // Navigate to the parent USB layout configuration to read the hardware manufacturer name
                        io_registry_entry_t parent;
                        if (IORegistryEntryGetParentEntry(device, kIOServicePlane, &parent) == KERN_SUCCESS) {
                            CFTypeRef propRef = IORegistryEntryCreateCFProperty(parent, CFSTR("Product Name"), kCFAllocatorDefault, 0);
                            if (propRef) {
                                char prodBuf[256];
                                if (CFStringGetCString((CFStringRef)propRef, prodBuf, sizeof(prodBuf), kCFStringEncodingUTF8)) {
                                    std::string prodStr(prodBuf);
                                    if (prodStr.find("serial") != std::string::npos) {
                                        isDongle = true;
                                    }
                                }
                                CFRelease(propRef);
                            }
                            IOObjectRelease(parent);
                        }

                        if (isDongle) {
                            availablePorts.push_back(pathStr);
                        }
                    }
                    CFRelease(pathRef);
                }
                IOObjectRelease(device);
            }
            IOIteratorRelease(iter);
        }
    }

#else
    // ==================================================
    // --- LINUX SYSFS HARDWARE FILTERING ---
    // ==================================================
    try {
        if (fs::exists("/dev")) {
            for (const auto& entry : fs::directory_iterator("/dev")) {
                std::string name = entry.path().filename().string();
                if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0) {
                    // Linux exposes hardware device strings completely through the sysfs filesystem tree
                    std::ifstream productFile("/sys/class/tty/" + name + "/device/../product");
                    if (productFile.is_open()) {
                        std::string productStr;
                        std::getline(productFile, productStr);
                        if (productStr.find("serial") != std::string::npos) {
                            availablePorts.push_back(entry.path().string());
                        }
                    }
                }
            }
        }
    }
    catch (...) {}
#endif

    // SAFE UX FALLBACK: Display an explicit warning item instead of dummy default strings 
    // that would cause a hardware crash loop if clicked.
    if (availablePorts.empty()) {
        availablePorts.push_back("No Dongle Found");
    }

    // Reset index bounds safety check
    if (selectedPortIdx >= static_cast<int>(availablePorts.size())) {
        selectedPortIdx = 0;
    }
}

UIManager::UIManager() : window(nullptr) {}

UIManager::~UIManager() {
    Cleanup();
}

bool UIManager::Initialize() {
    // 1. Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 2. Create the Window
    window = glfwCreateWindow(1280, 720, "Ganglion SSVEP", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD!" << std::endl;
        return false;
    }

    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float scaleFactor = (xscale + yscale) * 0.5f; // Average both axes

    // Clamp to avoid absurdly large UI on tiny screens (optional, but safe)
    if (scaleFactor < 1.0f) scaleFactor = 1.0f;
    if (scaleFactor > 3.0f) scaleFactor = 3.0f;


    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 4. Load Fonts
    const std::string fontPath = GetAssetPath("segoeui.ttf");
    std::vector<int> desiredSizes = { 16, 20, 24, 32, 40, 48, 60 };

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 3;

    for (int size : desiredSizes) {
        float scaledSize = (float)size * scaleFactor;

        ImFont* loadedFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), scaledSize, &fontConfig);
        if (loadedFont != nullptr) {
            fonts[size] = loadedFont;
        }
    }
    {
        ImFontConfig keyCfg = fontConfig;
        SSVEPKeyFont = io.Fonts->AddFontFromFileTTF(
            GetAssetPath("Nunito-ExtraBold.ttf").c_str(), 32.0f * scaleFactor, &keyCfg);
    }
    if (fonts.empty()) {
        io.Fonts->AddFontDefault(); // Fallback
    }

    ImGui::GetStyle().ScaleAllSizes(scaleFactor);
    glfwSetWindowSize(window, 1280 * scaleFactor, 720 * scaleFactor);

    // 5. Apply Theme
    SetupCustomTheme();
    UpdateAvailablePorts();

    const std::string assetsDir = GetAssetPath("");
    headObject.InitModel(GetAssetPath("SSVEP_headmodel.obj"), assetsDir);
    macHelpTextureID = LoadComponentTexture(GetAssetPath("mac_help.jpeg").c_str());
    qrCodeTextureID = LoadComponentTexture(GetAssetPath("qr_code.png").c_str());

    if (qrCodeTextureID != 0) {
        glBindTexture(GL_TEXTURE_2D, qrCodeTextureID);
        int w = 0, h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        if (w > 0 && h > 0) {
            std::vector<unsigned char> px(w * h * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (size_t i = 0; i < px.size(); i += 4) {
                px[i]   = 255 - px[i];     // R
                px[i+1] = 255 - px[i+1];   // G
                px[i+2] = 255 - px[i+2];   // B
            }
            glGenTextures(1, &qrCodeInvTextureID);
            glBindTexture(GL_TEXTURE_2D, qrCodeInvTextureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
    }

    m_dpiScale = scaleFactor;
    return true;
}

void UIManager::SetupCustomTheme() {
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 4.0f;

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.2f, 0.6f, 0.8f, 0.5f);
}

void UIManager::Run(std::atomic<bool>& isRunning) {
    auto lastDashboardRender = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window) && isRunning) {
        auto frameStart = std::chrono::high_resolution_clock::now();
        glfwPollEvents();

        const bool iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0;
        if (m_wasIconified && !iconified && showRecordPanel) {
            m_recenterRecordPanel = true;
        }
        m_wasIconified = iconified;

        const bool hudActive = hudOverlay.IsActive();
        const bool fastFlickerInMainWin = SSVEPTestMode || SSVEPPreviewEnabled;

        // Cap the heavy dashboard UI & graphs to ~30 FPS (33.3 ms) when HUD is active,
        // or ~60 FPS (16.6 ms) during normal browsing.
        // If Test Mode or Preview is active inside the main window, render every frame (165 Hz).
        const double dashIntervalMs = hudActive ? 33.3 : 16.6;
        std::chrono::duration<double, std::milli> sinceLastDash = frameStart - lastDashboardRender;
        const bool renderMainWindow = !iconified && (fastFlickerInMainWin || sinceLastDash.count() >= dashIntervalMs);

        // Advance SSVEP frame counter on every loop tick
        ++SSVEPFrame;

        if (renderMainWindow) {
            lastDashboardRender = frameStart;

            glfwMakeContextCurrent(window);
            // If HUD is active, let the HUD window pace VSYNC (set main window to 0 to avoid double-VSYNC halving)
            glfwSwapInterval(hudActive ? 0 : 1);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            RenderUI();

            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            if (SSVEPTestMode) {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            }
            else {
                glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
            }
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        // HUD Overlay ALWAYS renders every single hardware VBlank (165 Hz)
        if (hudActive) {
            hudOverlay.Render(iconified);
        }
        else if (!fastFlickerInMainWin) {
            // When no high-Hz flicker is active, sleep to cap the idle dashboard at ~60 FPS
            auto frameEnd = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed = frameEnd - frameStart;
            if (elapsed.count() < 16.0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(16.0 - elapsed.count())));
            }
        }
    }

    isRunning = false;
}

const char* ProPortVectorGetter(void* user_data, int idx) {
    auto* ports_vec = static_cast<std::vector<std::string>*>(user_data);

    // Safety check: ensure pointer is valid and index is in bounds
    if (!ports_vec || idx < 0 || idx >= static_cast<int>(ports_vec->size())) {
        return nullptr; // Returning null is safe; ImGui handles it gracefully
    }

    // Extract and return the raw C-string
    return (*ports_vec)[idx].c_str();
}

namespace {
float ClampedBoardSizeFrac(float sizeFrac) {
    return std::clamp(sizeFrac, 0.04f, 0.30f);
}

GLFWmonitor* GetContainingMonitor(GLFWwindow* window) {
    if (!window) return glfwGetPrimaryMonitor();
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    const int cx = wx + ww / 2;
    const int cy = wy + wh / 2;
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;
        if (cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) {
            return monitors[i];
        }
    }
    return glfwGetPrimaryMonitor();
}

void GetContainingMonitorSize(GLFWwindow* window, float* outW, float* outH) {
    float mw = 1920.0f;
    float mh = 1080.0f;
    GLFWmonitor* found = GetContainingMonitor(window);
    if (found) {
        const GLFWvidmode* mode = glfwGetVideoMode(found);
        if (mode && mode->width > 0 && mode->height > 0) {
            mw = static_cast<float>(mode->width);
            mh = static_cast<float>(mode->height);
        }
    }
    if (outW) *outW = mw;
    if (outH) *outH = mh;
}

int GetContainingMonitorRefresh(GLFWwindow* window) {
    GLFWmonitor* found = GetContainingMonitor(window);
    if (found) {
        const GLFWvidmode* mode = glfwGetVideoMode(found);
        if (mode && mode->refreshRate >= 30) return mode->refreshRate;
    }
    return 60;
}

void EnterSSVEPTestFullscreen(GLFWwindow* window, int* x, int* y, int* w, int* h) {
    if (!window) return;
    if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
        glfwRestoreWindow(window);
    }
    glfwGetWindowPos(window, x, y);
    glfwGetWindowSize(window, w, h);
    GLFWmonitor* mon = GetContainingMonitor(window);
    const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
    if (mon && mode && mode->width > 0 && mode->height > 0) {
        glfwSetWindowMonitor(window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else {
        glfwMaximizeWindow(window);
    }
}

void LeaveSSVEPTestFullscreen(GLFWwindow* window, int x, int y, int w, int h, int swapInterval) {
    if (!window) return;
    glfwSetWindowMonitor(window, nullptr, x, y, std::max(320, w), std::max(240, h), 0);
    glfwSwapInterval(swapInterval); // Restore the calculated fractional interval
}

void GenerateSSVEPTestSequence(int* seq, int* len, const bool visible[4]) {
    static std::mt19937 rng{ std::random_device{}() };
    int ids[4];
    int nVis = 0;
    for (int i = 0; i < 4; ++i) {
        if (visible && visible[i]) ids[nVis++] = i;
    }
    if (nVis <= 0) {
        *len = 0;
        return;
    }
    std::uniform_int_distribution<int> nDist(9, 12);
    std::uniform_int_distribution<int> bDist(0, nVis - 1);
    const int n = nDist(rng);
    *len = n;
    for (int i = 0; i < n; ++i) {
        seq[i] = ids[bDist(rng)];
    }
}

ImVec2 FitAspectRect(ImVec2 origin, ImVec2 size, float aspect, float pad, ImVec2* outSize) {
    const float aw = std::max(8.0f, size.x - 2.0f * pad);
    const float ah = std::max(8.0f, size.y - 2.0f * pad);
    float w = aw;
    float h = ah;
    if (aspect > 0.01f) {
        if (aw / ah > aspect) {
            h = ah;
            w = h * aspect;
        }
        else {
            w = aw;
            h = w / aspect;
        }
    }
    if (outSize) *outSize = ImVec2(w, h);
    return ImVec2(origin.x + pad + 0.5f * (aw - w), origin.y + pad + 0.5f * (ah - h));
}

void DrawSlideToggle(const char* id, bool* value, float dpi) {
    const float h = 28.0f * dpi;
    const float w = 52.0f * dpi;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton(id, ImVec2(w, h))) {
        *value = !(*value);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 track = *value ? IM_COL32(46, 160, 67, 255) : IM_COL32(176, 176, 176, 255);
    draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), track, h * 0.5f);
    const float knobR = h * 0.5f - 3.0f * dpi;
    const float knobX = *value ? (pos.x + w - h * 0.5f) : (pos.x + h * 0.5f);
    const float knobY = pos.y + h * 0.5f;
    draw->AddCircleFilled(ImVec2(knobX, knobY), knobR, IM_COL32(255, 255, 255, 255));
}

void DrawSSVEPPreviewBoards(ImVec2 origin, ImVec2 size, bool previewOn, float dpi, ImFont* keyFont,
    SSVEPBoardLayout* layout, bool* layoutInit, float monitorW, float monitorH,
    int refreshHz, std::uint64_t frame, unsigned int qrTex, unsigned int qrInvTex) {
    if (size.x < 8.0f || size.y < 8.0f || !layout || !layoutInit) return;

    const float monitorPad = 10.0f * dpi;
    const float monitorAspect = (monitorH > 0.01f) ? (monitorW / monitorH) : (16.0f / 9.0f);
    ImVec2 monSize = size;
    const ImVec2 monOrigin = FitAspectRect(origin, size, monitorAspect, monitorPad, &monSize);

    const float inset = 8.0f * dpi;
    const float iconGap = 6.0f * dpi;

    struct KeyArt {
        unsigned tex = 0;
        int w = 0;
        int h = 0;
    };
    static KeyArt artShort, artLong, artMouse[3];
    static bool texTried = false;
    if (!texTried) {
        texTried = true;
        artShort.tex = LoadComponentTexture(GetAssetPath("keys/shortkey.png").c_str(), &artShort.w, &artShort.h);
        artLong.tex = LoadComponentTexture(GetAssetPath("keys/longkey.png").c_str(), &artLong.w, &artLong.h);
        artMouse[0].tex = LoadComponentTexture(GetAssetPath("keys/mouse_1.png").c_str(), &artMouse[0].w, &artMouse[0].h);
        artMouse[1].tex = LoadComponentTexture(GetAssetPath("keys/mouse_2.png").c_str(), &artMouse[1].w, &artMouse[1].h);
        artMouse[2].tex = LoadComponentTexture(GetAssetPath("keys/mouse_3.png").c_str(), &artMouse[2].w, &artMouse[2].h);
    }

    const float lw = (artLong.w > 0) ? static_cast<float>(artLong.w) : 388.0f;
    const float lh = (artLong.h > 0) ? static_cast<float>(artLong.h) : 244.0f;

    float sides[4];
    float footprints[4];
    for (int i = 0; i < 4; ++i) {
        layout->sizeFrac[i] = ClampedBoardSizeFrac(layout->sizeFrac[i]);
        sides[i] = std::max(8.0f, monSize.x * layout->sizeFrac[i]);
        float dw = 0.0f, dh = 0.0f;
        KeyIconCatalog::DisplaySize(lw, lh, sides[i], lw, lh, dw, dh);
        footprints[i] = sides[i] + iconGap + dh;
    }

    if (!*layoutInit) {
        const ImVec2 defaults[4] = {
            ImVec2(monOrigin.x + inset, monOrigin.y + inset),
            ImVec2(monOrigin.x + monSize.x - inset - sides[1], monOrigin.y + inset),
            ImVec2(monOrigin.x + inset, monOrigin.y + monSize.y - inset - footprints[2]),
            ImVec2(monOrigin.x + monSize.x - inset - sides[3], monOrigin.y + monSize.y - inset - footprints[3])
        };
        for (int i = 0; i < 4; ++i) {
            layout->nx[i] = (defaults[i].x - monOrigin.x) / monSize.x;
            layout->ny[i] = (defaults[i].y - monOrigin.y) / monSize.y;
        }
        *layoutInit = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

    const ImVec2 monMax(monOrigin.x + monSize.x, monOrigin.y + monSize.y);
    draw->AddRectFilled(monOrigin, monMax, IM_COL32(245, 245, 245, 70));
    draw->AddRect(monOrigin, monMax, IM_COL32(70, 70, 70, 255), 0.0f, 0, 5.0f * dpi);

    ImVec2 corners[4];
    const ImVec2 backupCursorPos = ImGui::GetCursorPos();
    const float xMin = monOrigin.x;
    const float yMin = monOrigin.y;
    for (int i = 0; i < 4; ++i) {
        const float xMax = monOrigin.x + std::max(0.0f, monSize.x - sides[i]);
        const float yMax = monOrigin.y + std::max(0.0f, monSize.y - footprints[i]);
        float x = monOrigin.x + layout->nx[i] * monSize.x;
        float y = monOrigin.y + layout->ny[i] * monSize.y;
        x = std::clamp(x, xMin, xMax);
        y = std::clamp(y, yMin, yMax);

        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushID(100 + i);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##SSVEPBoardDrag", ImVec2(sides[i], sides[i]));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            x += ImGui::GetIO().MouseDelta.x;
            y += ImGui::GetIO().MouseDelta.y;
            x = std::clamp(x, xMin, xMax);
            y = std::clamp(y, yMin, yMax);
        }
        ImGui::PopID();

        layout->nx[i] = (monSize.x > 0.01f) ? ((x - monOrigin.x) / monSize.x) : 0.0f;
        layout->ny[i] = (monSize.y > 0.01f) ? ((y - monOrigin.y) / monSize.y) : 0.0f;
        corners[i] = ImVec2(x, y);
    }
    ImGui::SetCursorPos(backupCursorPos);

    auto drawVisButton = [&](int i) {
        const float btnW = 56.0f * dpi;
        const float btnH = ImGui::GetFrameHeight();
        const float gap = 4.0f * dpi;
        const bool towardRight = layout->nx[i] < 0.5f;
        float hx = towardRight ? (corners[i].x + sides[i] + gap) : (corners[i].x - btnW - gap);
        float hy = corners[i].y;
        hx = std::clamp(hx, monOrigin.x, monOrigin.x + std::max(0.0f, monSize.x - btnW));
        hy = std::clamp(hy, monOrigin.y, monOrigin.y + std::max(0.0f, monSize.y - btnH));
        ImGui::SetCursorScreenPos(ImVec2(hx, hy));
        ImGui::PushID(200 + i);
        const bool vis = layout->visible[i];
        if (vis) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.72f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.82f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.36f, 0.60f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.40f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.32f, 0.32f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        if (ImGui::Button(vis ? "Hide" : "Show", ImVec2(btnW, btnH))) {
            layout->visible[i] = !layout->visible[i];
        }
        ImGui::PopStyleColor(4);
        ImGui::PopID();
    };

    auto resizeBoard = [&](int i) {
        const float grip = std::clamp(16.0f * dpi, 12.0f, std::max(12.0f, sides[i] * 0.30f));
        const float iconK = (lw > 1.0f) ? (0.5f * lh / lw) : 0.3f;
        
        // Loop over 4 corners: 0=Top-Left, 1=Top-Right, 2=Bottom-Left, 3=Bottom-Right
        for (int c = 0; c < 4; ++c) {
            ImVec2 gripPos;
            ImGuiMouseCursor cursor;
            
            if (c == 0) { // Top-Left
                gripPos = ImVec2(corners[i].x, corners[i].y);
                cursor = ImGuiMouseCursor_ResizeNWSE;
            } else if (c == 1) { // Top-Right
                gripPos = ImVec2(corners[i].x + sides[i] - grip, corners[i].y);
                cursor = ImGuiMouseCursor_ResizeNESW;
            } else if (c == 2) { // Bottom-Left
                gripPos = ImVec2(corners[i].x, corners[i].y + sides[i] - grip);
                cursor = ImGuiMouseCursor_ResizeNESW;
            } else { // Bottom-Right
                gripPos = ImVec2(corners[i].x + sides[i] - grip, corners[i].y + sides[i] - grip);
                cursor = ImGuiMouseCursor_ResizeNWSE;
            }
            
            ImGui::SetCursorScreenPos(gripPos);
            ImGui::PushID(3000 + i * 4 + c);
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##SSVEPBoardResize", ImVec2(grip, grip));
            
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot) ImGui::SetMouseCursor(cursor);
            
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 mp = ImGui::GetIO().MousePos;
                float proposed = sides[i];
                float maxAllowed = monSize.x * 0.30f; // Global max size constraint
                
                // Calculate proposed size and dynamic constraints based on the opposite anchor corner
                if (c == 0) { // Dragging Top-Left, anchored at Bottom-Right
                    float brX = corners[i].x + sides[i];
                    float brY = corners[i].y + sides[i];
                    proposed = std::max(brX - mp.x, brY - mp.y);
                    maxAllowed = std::min(maxAllowed, brX - monOrigin.x);
                    maxAllowed = std::min(maxAllowed, brY - monOrigin.y);
                    maxAllowed = std::min(maxAllowed, std::max(8.0f, (monOrigin.y + monSize.y - brY - iconGap) / iconK));
                } else if (c == 1) { // Dragging Top-Right, anchored at Bottom-Left
                    float blX = corners[i].x;
                    float blY = corners[i].y + sides[i];
                    proposed = std::max(mp.x - blX, blY - mp.y);
                    maxAllowed = std::min(maxAllowed, (monOrigin.x + monSize.x) - blX);
                    maxAllowed = std::min(maxAllowed, blY - monOrigin.y);
                    maxAllowed = std::min(maxAllowed, std::max(8.0f, (monOrigin.y + monSize.y - blY - iconGap) / iconK));
                } else if (c == 2) { // Dragging Bottom-Left, anchored at Top-Right
                    float trX = corners[i].x + sides[i];
                    float trY = corners[i].y;
                    proposed = std::max(trX - mp.x, mp.y - trY);
                    maxAllowed = std::min(maxAllowed, trX - monOrigin.x);
                    maxAllowed = std::min(maxAllowed, std::max(8.0f, (monOrigin.y + monSize.y - trY - iconGap) / (1.0f + iconK)));
                } else { // Dragging Bottom-Right, anchored at Top-Left
                    float tlX = corners[i].x;
                    float tlY = corners[i].y;
                    proposed = std::max(mp.x - tlX, mp.y - tlY);
                    maxAllowed = std::min(maxAllowed, (monOrigin.x + monSize.x) - tlX);
                    maxAllowed = std::min(maxAllowed, std::max(8.0f, (monOrigin.y + monSize.y - tlY - iconGap) / (1.0f + iconK)));
                }
                
                const float minSide = std::min(maxAllowed, std::max(8.0f, monSize.x * 0.04f));
                float newSide = std::clamp(proposed, minSide, maxAllowed);
                
                // Shift the top-left X/Y coordinates depending on which way the box grew
                if (c == 0) {
                    corners[i].x += (sides[i] - newSide);
                    corners[i].y += (sides[i] - newSide);
                } else if (c == 1) {
                    corners[i].y += (sides[i] - newSide);
                } else if (c == 2) {
                    corners[i].x += (sides[i] - newSide);
                }
                
                // Apply final layout updates
                sides[i] = newSide;
                layout->sizeFrac[i] = (monSize.x > 0.01f) ? (newSide / monSize.x) : 0.10f;
                layout->nx[i] = (monSize.x > 0.01f) ? ((corners[i].x - monOrigin.x) / monSize.x) : 0.0f;
                layout->ny[i] = (monSize.y > 0.01f) ? ((corners[i].y - monOrigin.y) / monSize.y) : 0.0f;
            }
            ImGui::PopID();
            
            // Draw visual triangle grips for each respective corner
            ImVec2 tip, p1, p2;
            if (c == 0) { // Top-Left Grip
                tip = ImVec2(corners[i].x, corners[i].y);
                p1 = ImVec2(tip.x + grip, tip.y);
                p2 = ImVec2(tip.x, tip.y + grip);
            } else if (c == 1) { // Top-Right Grip
                tip = ImVec2(corners[i].x + sides[i], corners[i].y);
                p1 = ImVec2(tip.x - grip, tip.y);
                p2 = ImVec2(tip.x, tip.y + grip);
            } else if (c == 2) { // Bottom-Left Grip
                tip = ImVec2(corners[i].x, corners[i].y + sides[i]);
                p1 = ImVec2(tip.x + grip, tip.y);
                p2 = ImVec2(tip.x, tip.y - grip);
            } else { // Bottom-Right Grip
                tip = ImVec2(corners[i].x + sides[i], corners[i].y + sides[i]);
                p1 = ImVec2(tip.x - grip, tip.y);
                p2 = ImVec2(tip.x, tip.y - grip);
            }
            
            draw->AddTriangleFilled(tip, p1, p2, hot ? IM_COL32(255, 210, 70, 240) : IM_COL32(24, 24, 24, 220));
            const float stroke = std::max(1.0f, 1.5f * dpi);
            draw->AddLine(p1, tip, IM_COL32(255, 255, 255, 235), stroke);
            draw->AddLine(tip, p2, IM_COL32(255, 255, 255, 235), stroke);
        }
    };

    if (previewOn) {

        double timeNow = glfwGetTime();

        for (int i = 0; i < 4; ++i) {
            const ImVec2 a = corners[i];
            const ImVec2 b = ImVec2(a.x + sides[i], a.y + sides[i]);
            if (!layout->visible[i]) {
                draw->AddRect(a, b, IM_COL32(160, 160, 160, 160), 0.0f, 0, 1.5f * dpi);
                continue;
            }

            // 1. Time-Based Absolute Frequency Math
            bool contrast = (layout->patternMode == 1 && layout->contrastMode);
            double screenTarget = contrast ? (static_cast<double>(layout->customHz[i]) / 2.0) : static_cast<double>(layout->customHz[i]);
            double phase = std::fmod(timeNow * screenTarget, 1.0);
            bool on = phase < 0.5;
            
            // 2. Alpha Interpolation
            float alpha = on ? 1.0f : 0.0f;
            if (layout->smoothMode) {
                alpha = 0.5f - 0.5f * static_cast<float>(std::cos(2.0 * 3.14159265358979 * phase));
            }

            ImU8 aVal = static_cast<ImU8>(alpha * 255.0f);
            ImU8 invVal = static_cast<ImU8>((1.0f - alpha) * 255.0f);

            // 3. Texture / Contrast Reversal Drawing
            if (layout->patternMode == 1 && qrTex) {
                if (layout->contrastMode && qrInvTex) {
                    draw->AddImage((ImTextureID)(intptr_t)qrTex, a, b, ImVec2(0,0), ImVec2(1,1), IM_COL32(255, 255, 255, aVal));
                    draw->AddImage((ImTextureID)(intptr_t)qrInvTex, a, b, ImVec2(0,0), ImVec2(1,1), IM_COL32(255, 255, 255, invVal));
                } else {
                    draw->AddImage((ImTextureID)(intptr_t)qrTex, a, b, ImVec2(0,0), ImVec2(1,1), IM_COL32(aVal, aVal, aVal, 255));
                }
            } else {
                draw->AddRectFilled(a, b, IM_COL32(aVal, aVal, aVal, 255));
            }
            draw->AddRect(a, b, IM_COL32(80, 80, 80, 255));
        }

        auto& inputMgr = InputManager::GetInstance();
        for (int i = 0; i < 4; ++i) {
            if (!layout->visible[i]) continue;
            const ImGuiKey key = inputMgr.GetKey(i);
            const auto kind = KeyIconCatalog::KindFor(key);
            const KeyArt* art = nullptr;
            switch (kind) {
            case KeyIconCatalog::Kind::ShortKey:    art = &artShort; break;
            case KeyIconCatalog::Kind::LongKey:     art = &artLong; break;
            case KeyIconCatalog::Kind::MouseLeft:   art = &artMouse[0]; break;
            case KeyIconCatalog::Kind::MouseRight:  art = &artMouse[1]; break;
            case KeyIconCatalog::Kind::MouseMiddle: art = &artMouse[2]; break;
            default: break;
            }
            if (!art || !art->tex || art->w <= 0) continue;

            float dw = 0.0f, dh = 0.0f;
            KeyIconCatalog::DisplaySize(
                static_cast<float>(art->w), static_cast<float>(art->h), sides[i], lw, lh, dw, dh);
            const float dx = corners[i].x + 0.5f * (sides[i] - dw);
            const float dy = corners[i].y + sides[i] + iconGap;
            draw->AddImage(ImTextureRef((ImTextureID)(intptr_t)art->tex), ImVec2(dx, dy), ImVec2(dx + dw, dy + dh));

            const std::string caption = KeyIconCatalog::Caption(key);
            if (!caption.empty()) {
                const bool longKey = (kind == KeyIconCatalog::Kind::LongKey);
                const float maxW = dw * (longKey ? 0.62f : 0.50f);
                const float maxH = dh * (longKey ? 0.22f : 0.34f);
                float fontSize = maxH;
                ImFont* font = keyFont ? keyFont : ImGui::GetFont();
                ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, caption.c_str());
                if (ts.x > maxW && ts.x > 1.0f) {
                    fontSize *= maxW / ts.x;
                    ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, caption.c_str());
                }
                const ImVec2 tp(
                    dx + 0.5f * dw - 0.5f * ts.x,
                    dy + dh * 0.38f - 0.5f * ts.y);
                draw->AddText(font, fontSize, tp, IM_COL32(26, 26, 26, 255), caption.c_str());
            }
        }
        for (int i = 0; i < 4; ++i) drawVisButton(i);
        for (int i = 0; i < 4; ++i) resizeBoard(i);
        ImGui::SetCursorPos(backupCursorPos);
        draw->PopClipRect();
        return;
    }

    for (int i = 0; i < 4; ++i) {
        const ImVec2 a = corners[i];
        const ImVec2 b = ImVec2(a.x + sides[i], a.y + sides[i]);
        const ImU32 col = layout->visible[i] ? IM_COL32(180, 180, 180, 255) : IM_COL32(140, 140, 140, 140);
        draw->AddRect(a, b, col);
    }
    draw->PopClipRect();

    auto& inputMgr = InputManager::GetInstance();
    inputMgr.Update();

    const float btnW = 45.0f * dpi;
    const float btnH = ImGui::GetFrameHeight();
    const float inputW = 36.0f * dpi;

    for (int i = 0; i < 4; ++i) {
        const ImVec2 center = ImVec2(corners[i].x + sides[i] * 0.5f, corners[i].y + sides[i] * 0.5f);
        ImGui::SetCursorScreenPos(ImVec2(center.x - btnW * 0.5f, center.y - btnH * 0.5f));
        ImGui::PushID(i);

        const bool thisBinding = (inputMgr.GetBindingBoard() == i);
        const bool otherBinding = inputMgr.IsBinding() && !thisBinding;

        if (thisBinding) {
            ImGui::BeginDisabled();
            ImGui::Button("...", ImVec2(btnW, btnH));
            ImGui::EndDisabled();
        }
        else {
            if (otherBinding) ImGui::BeginDisabled();
            if (ImGui::Button("Set", ImVec2(btnW, btnH))) {
                OSInputSimulator::ReleaseAllKey();
                inputMgr.StartBinding(i);
            }
            if (otherBinding) ImGui::EndDisabled();
        }

        std::string keyLabel = thisBinding ? "..." : inputMgr.GetKeyName(i);
        char keyBuf[64];
        snprintf(keyBuf, sizeof(keyBuf), "Key: %s", keyLabel.c_str());
        const ImVec2 keySize = ImGui::CalcTextSize(keyBuf);
        const ImVec2 keyPos = ImVec2(center.x - keySize.x * 0.5f, center.y + btnH * 0.5f + 2.0f * dpi);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(keyPos.x - 3.0f * dpi, keyPos.y - 1.0f * dpi),
            ImVec2(keyPos.x + keySize.x + 3.0f * dpi, keyPos.y + keySize.y + 1.0f * dpi),
            IM_COL32(255, 255, 255, 210), 3.0f * dpi);
        ImGui::SetCursorScreenPos(keyPos);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
        ImGui::TextUnformatted(keyBuf);
        ImGui::PopStyleColor();

        char freqLabelBuf[32];
        snprintf(freqLabelBuf, sizeof(freqLabelBuf), "Freq: ");
        const ImVec2 freqLabelSize = ImGui::CalcTextSize(freqLabelBuf);
        const ImVec2 hzSize = ImGui::CalcTextSize(" Hz");

        const float totalBlockW = freqLabelSize.x + inputW + hzSize.x + 4.0f * dpi;
        
        // Position frequency input directly below the key label
        const ImVec2 freqPos = ImVec2(
            corners[i].x + (sides[i] - totalBlockW) * 0.5f, // Centered horizontally with the box
            corners[i].y + sides[i] + 8.0f * dpi            // Bottom edge + 8px scaled margin
        );
            
        // Draw background for frequency input 
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(freqPos.x - 4.0f * dpi, freqPos.y - 2.0f * dpi),
            ImVec2(freqPos.x + totalBlockW + 4.0f * dpi, freqPos.y + ImGui::GetFrameHeight() + 2.0f * dpi),
            IM_COL32(255, 255, 255, 210), 3.0f * dpi);

        ImGui::SetCursorScreenPos(freqPos);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
        ImGui::TextUnformatted(freqLabelBuf);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        
        // Apply the narrower width
        ImGui::SetNextItemWidth(inputW);
        if (ImGui::InputFloat("##freqInput", &layout->customHz[i], 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_CharsDecimal)) {
            // Clamp to requested 5 - 30 Hz range
            layout->customHz[i] = std::clamp(layout->customHz[i], 5.0f, 30.0f);
        }
        
        ImGui::SameLine(0, 4.0f * dpi);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
        ImGui::TextUnformatted("Hz");
        ImGui::PopStyleColor();

        ImGui::PopID();
        drawVisButton(i);
    }
    for (int i = 0; i < 4; ++i) resizeBoard(i);
    ImGui::SetCursorPos(backupCursorPos);
}

bool DrawSSVEPTestScreen(float dpi, ImFont* keyFont, const SSVEPBoardLayout& layout,
    const int* seq, int seqLen, int* seqIndex, int* seqCorrect, bool* seqHit, bool* seqDone,
    int refreshHz, std::uint64_t frame, unsigned int qrTex, unsigned int qrInvTex) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::Begin("SSVEP Test", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings);

    const float iconGap = 6.0f * dpi;

    struct KeyArt {
        unsigned tex = 0;
        int w = 0;
        int h = 0;
    };
    static KeyArt artShort, artLong, artMouse[3];
    static bool texTried = false;
    if (!texTried) {
        texTried = true;
        artShort.tex = LoadComponentTexture(GetAssetPath("keys/shortkey.png").c_str(), &artShort.w, &artShort.h);
        artLong.tex = LoadComponentTexture(GetAssetPath("keys/longkey.png").c_str(), &artLong.w, &artLong.h);
        artMouse[0].tex = LoadComponentTexture(GetAssetPath("keys/mouse_1.png").c_str(), &artMouse[0].w, &artMouse[0].h);
        artMouse[1].tex = LoadComponentTexture(GetAssetPath("keys/mouse_2.png").c_str(), &artMouse[1].w, &artMouse[1].h);
        artMouse[2].tex = LoadComponentTexture(GetAssetPath("keys/mouse_3.png").c_str(), &artMouse[2].w, &artMouse[2].h);
    }

    const float lw = (artLong.w > 0) ? static_cast<float>(artLong.w) : 388.0f;
    const float lh = (artLong.h > 0) ? static_cast<float>(artLong.h) : 244.0f;

    auto boardSide = [&](int i) {
        return std::max(8.0f, display.x * ClampedBoardSizeFrac(layout.sizeFrac[i]));
    };
    auto boardFootprint = [&](int i, float side) {
        float dw = 0.0f, dh = 0.0f;
        KeyIconCatalog::DisplaySize(lw, lh, side, lw, lh, dw, dh);
        return side + iconGap + dh;
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();

    auto& inputMgr = InputManager::GetInstance();
    double timeNow = glfwGetTime();

    for (int i = 0; i < 4; ++i) {
        if (!layout.visible[i]) continue;
        const float side = boardSide(i);
        const float xMax = std::max(0.0f, display.x - side);
        const float yMax = std::max(0.0f, display.y - boardFootprint(i, side));
        float x = std::clamp(layout.nx[i] * display.x, 0.0f, xMax);
        float y = std::clamp(layout.ny[i] * display.y, 0.0f, yMax);
        const ImVec2 a(x, y);
        const ImVec2 b(x + side, y + side);
        
        // 1. Time-Based Absolute Frequency Math
        bool contrast = (layout.patternMode == 1 && layout.contrastMode);
        double screenTarget = contrast ? (static_cast<double>(layout.customHz[i]) / 2.0) : static_cast<double>(layout.customHz[i]);
        double phase = std::fmod(timeNow * screenTarget, 1.0);
        bool on = phase < 0.5;
        
        // 2. Alpha Interpolation
        float alpha = on ? 1.0f : 0.0f;
        if (layout.smoothMode) {
            alpha = 0.5f - 0.5f * static_cast<float>(std::cos(2.0 * 3.14159265358979 * phase));
        }

        ImU8 aVal = static_cast<ImU8>(alpha * 255.0f);
        ImU8 invVal = static_cast<ImU8>((1.0f - alpha) * 255.0f);

        // 3. Texture / Contrast Reversal Drawing
        if (layout.patternMode == 1 && qrTex) {
            if (layout.contrastMode && qrInvTex) {
                draw->AddImage((ImTextureID)(intptr_t)qrTex, a, b, ImVec2(0,0), ImVec2(1,1), IM_COL32(255, 255, 255, aVal));
                draw->AddImage((ImTextureID)(intptr_t)qrInvTex, a, b, ImVec2(0,0), ImVec2(1,1), IM_COL32(255, 255, 255, invVal));
            } else {
                draw->AddImage((ImTextureID)(intptr_t)qrTex, a, b, ImVec2(0,0), ImVec2(1,1), IM_COL32(aVal, aVal, aVal, 255));
            }
        } else {
            draw->AddRectFilled(a, b, IM_COL32(aVal, aVal, aVal, 255));
        }

        draw->AddRect(a, b, IM_COL32(80, 80, 80, 255));
    }

    for (int i = 0; i < 4; ++i) {
        if (!layout.visible[i]) continue;
        const float side = boardSide(i);
        const float xMax = std::max(0.0f, display.x - side);
        const float yMax = std::max(0.0f, display.y - boardFootprint(i, side));
        float x = std::clamp(layout.nx[i] * display.x, 0.0f, xMax);
        float y = std::clamp(layout.ny[i] * display.y, 0.0f, yMax);
        const ImGuiKey key = inputMgr.GetKey(i);
        const auto kind = KeyIconCatalog::KindFor(key);
        const KeyArt* art = nullptr;
        switch (kind) {
        case KeyIconCatalog::Kind::ShortKey:    art = &artShort; break;
        case KeyIconCatalog::Kind::LongKey:     art = &artLong; break;
        case KeyIconCatalog::Kind::MouseLeft:   art = &artMouse[0]; break;
        case KeyIconCatalog::Kind::MouseRight:  art = &artMouse[1]; break;
        case KeyIconCatalog::Kind::MouseMiddle: art = &artMouse[2]; break;
        default: break;
        }
        if (!art || !art->tex || art->w <= 0) continue;

        float dw = 0.0f, dh = 0.0f;
        KeyIconCatalog::DisplaySize(
            static_cast<float>(art->w), static_cast<float>(art->h), side, lw, lh, dw, dh);
        const float dx = x + 0.5f * (side - dw);
        const float dy = y + side + iconGap;
        draw->AddImage(ImTextureRef((ImTextureID)(intptr_t)art->tex), ImVec2(dx, dy), ImVec2(dx + dw, dy + dh));

        const std::string caption = KeyIconCatalog::Caption(key);
        if (!caption.empty()) {
            const bool longKey = (kind == KeyIconCatalog::Kind::LongKey);
            const float maxW = dw * (longKey ? 0.62f : 0.50f);
            const float maxH = dh * (longKey ? 0.22f : 0.34f);
            float fontSize = maxH;
            ImFont* font = keyFont ? keyFont : ImGui::GetFont();
            ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, caption.c_str());
            if (ts.x > maxW && ts.x > 1.0f) {
                fontSize *= maxW / ts.x;
                ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, caption.c_str());
            }
            const ImVec2 tp(
                dx + 0.5f * dw - 0.5f * ts.x,
                dy + dh * 0.38f - 0.5f * ts.y);
            draw->AddText(font, fontSize, tp, IM_COL32(26, 26, 26, 255), caption.c_str());
        }
    }

    const char* prompt = "Try to enter the sequence:";
    ImFont* promptFont = ImGui::GetFont();
    const float sidePad = 36.0f * dpi;
    const float maxRowW = std::max(64.0f, display.x - 2.0f * sidePad);
    float promptSize = std::min(22.0f * dpi, display.y * 0.035f);
    promptSize = std::max(14.0f * dpi, promptSize);
    ImVec2 promptTs = promptFont->CalcTextSizeA(promptSize, FLT_MAX, 0.0f, prompt);
    if (promptTs.x > maxRowW && promptTs.x > 1.0f) {
        promptSize *= maxRowW / promptTs.x;
        promptTs = promptFont->CalcTextSizeA(promptSize, FLT_MAX, 0.0f, prompt);
    }

    const float btnW = 180.0f * dpi;
    const float btnH = 52.0f * dpi;
    const float gapImg = 10.0f * dpi;
    const float firstGrow = 1.45f;
    const int nSeq = (seq && seqLen > 0) ? std::clamp(seqLen, 0, 12) : 0;
    const int cur = (seqIndex && nSeq > 0) ? std::clamp(*seqIndex, 0, nSeq) : 0;
    const bool finished = seqDone && *seqDone;

    auto artForBoard = [&](int board, const KeyArt*& art, ImGuiKey& key, KeyIconCatalog::Kind& kind) {
        key = inputMgr.GetKey(board);
        kind = KeyIconCatalog::KindFor(key);
        art = nullptr;
        switch (kind) {
        case KeyIconCatalog::Kind::ShortKey:    art = &artShort; break;
        case KeyIconCatalog::Kind::LongKey:     art = &artLong; break;
        case KeyIconCatalog::Kind::MouseLeft:   art = &artMouse[0]; break;
        case KeyIconCatalog::Kind::MouseRight:  art = &artMouse[1]; break;
        case KeyIconCatalog::Kind::MouseMiddle: art = &artMouse[2]; break;
        default: art = &artShort; kind = KeyIconCatalog::Kind::ShortKey; break;
        }
        if (!art || !art->tex || art->w <= 0) {
            art = &artShort;
            kind = KeyIconCatalog::Kind::ShortKey;
        }
    };

    float widths[12] = {};
    float heights[12] = {};
    float baseH = std::min(56.0f * dpi, display.y * 0.08f);
    const float boardWForH = (lh > 1.0f) ? (baseH * 2.0f * lw / lh) : (baseH * 2.0f);
    for (int i = 0; i < nSeq; ++i) {
        const KeyArt* art = nullptr;
        ImGuiKey key = ImGuiKey_None;
        KeyIconCatalog::Kind kind = KeyIconCatalog::Kind::None;
        artForBoard(seq[i], art, key, kind);
        float dw = 0.0f, dh = 0.0f;
        KeyIconCatalog::DisplaySize(
            static_cast<float>(std::max(art->w, 1)), static_cast<float>(std::max(art->h, 1)),
            boardWForH, lw, lh, dw, dh);
        const float grow = (!finished && i == cur) ? firstGrow : 1.0f;
        widths[i] = dw * grow;
        heights[i] = dh * grow;
    }
    float rowW = 0.0f;
    float rowH = 0.0f;
    for (int i = 0; i < nSeq; ++i) {
        rowW += widths[i];
        if (i + 1 < nSeq) rowW += gapImg;
        rowH = std::max(rowH, heights[i]);
    }
    const float promptGap = 10.0f * dpi;
    const float scoreGap = 8.0f * dpi;
    const float btnGap = 18.0f * dpi;
    float scoreSize = std::min(20.0f * dpi, display.y * 0.032f);
    scoreSize = std::max(14.0f * dpi, scoreSize);
    char scoreBuf[64] = "";
    if (finished && nSeq > 0 && seqCorrect) {
        const int ok = std::clamp(*seqCorrect, 0, nSeq);
        const int pct = static_cast<int>(std::lround(100.0 * static_cast<double>(ok) / static_cast<double>(nSeq)));
        std::snprintf(scoreBuf, sizeof(scoreBuf), "%d/%d  %d%%", ok, nSeq, pct);
    }
    ImVec2 scoreTs = promptFont->CalcTextSizeA(scoreSize, FLT_MAX, 0.0f, finished ? scoreBuf : "0/12  100%");
    float blockH = promptTs.y + promptGap + rowH + scoreGap + scoreTs.y + btnGap + btnH;
    float fit = 1.0f;
    if (rowW > maxRowW && rowW > 1.0f) fit = std::min(fit, maxRowW / rowW);
    const float maxBlockH = display.y * 0.42f;
    if (blockH > maxBlockH && blockH > 1.0f) fit = std::min(fit, maxBlockH / blockH);
    if (fit < 0.999f) {
        promptSize *= fit;
        promptTs = promptFont->CalcTextSizeA(promptSize, FLT_MAX, 0.0f, prompt);
        rowW = 0.0f;
        rowH = 0.0f;
        for (int i = 0; i < nSeq; ++i) {
            widths[i] *= fit;
            heights[i] *= fit;
            rowW += widths[i];
            if (i + 1 < nSeq) rowW += gapImg * fit;
            rowH = std::max(rowH, heights[i]);
        }
        blockH = promptTs.y + promptGap * fit + rowH + scoreGap * fit + scoreTs.y * fit + btnGap * fit + btnH;
        scoreSize *= fit;
        scoreTs = promptFont->CalcTextSizeA(scoreSize, FLT_MAX, 0.0f, finished ? scoreBuf : "0/12  100%");
    }

    const float g = (fit < 0.999f) ? fit : 1.0f;
    const float blockTop = 0.5f * (display.y - blockH);
    const float promptX = 0.5f * (display.x - promptTs.x);
    const float promptY = blockTop;
    draw->AddText(promptFont, promptSize, ImVec2(promptX, promptY), IM_COL32(240, 240, 240, 255), prompt);

    float imgX = 0.5f * (display.x - rowW);
    const float imgMidY = promptY + promptTs.y + promptGap * g + 0.5f * rowH;
    for (int i = 0; i < nSeq; ++i) {
        const KeyArt* art = nullptr;
        ImGuiKey key = ImGuiKey_None;
        KeyIconCatalog::Kind kind = KeyIconCatalog::Kind::None;
        artForBoard(seq[i], art, key, kind);
        const float dw = widths[i];
        const float dh = heights[i];
        const float x0 = imgX;
        const float y0 = imgMidY - 0.5f * dh;

        ImU32 tint = IM_COL32(255, 255, 255, 72);
        if (i < cur) {
            const bool ok = seqHit && seqHit[i];
            tint = ok ? IM_COL32(140, 230, 150, 230) : IM_COL32(230, 110, 110, 230);
        }
        else if (!finished && i == cur) {
            tint = IM_COL32(255, 255, 255, 255);
        }

        draw->AddImage(
            ImTextureRef((ImTextureID)(intptr_t)art->tex),
            ImVec2(x0, y0), ImVec2(x0 + dw, y0 + dh),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            tint);

        const unsigned char capA = (unsigned char)((tint >> IM_COL32_A_SHIFT) & 0xFF);
        const std::string caption = KeyIconCatalog::Caption(key);
        if (!caption.empty() && art->tex) {
            const bool longKey = (kind == KeyIconCatalog::Kind::LongKey);
            const float maxW = dw * (longKey ? 0.62f : 0.50f);
            const float maxH = dh * (longKey ? 0.22f : 0.34f);
            float capSize = maxH;
            ImFont* font = keyFont ? keyFont : ImGui::GetFont();
            ImVec2 ts = font->CalcTextSizeA(capSize, FLT_MAX, 0.0f, caption.c_str());
            if (ts.x > maxW && ts.x > 1.0f) {
                capSize *= maxW / ts.x;
                ts = font->CalcTextSizeA(capSize, FLT_MAX, 0.0f, caption.c_str());
            }
            const ImVec2 tp(x0 + 0.5f * dw - 0.5f * ts.x, y0 + dh * 0.38f - 0.5f * ts.y);
            draw->AddText(font, capSize, tp, IM_COL32(26, 26, 26, capA), caption.c_str());
        }
        imgX += dw + gapImg * g;
    }

    if (finished && scoreBuf[0] != '\0') {
        const float scoreY = imgMidY + 0.5f * rowH + scoreGap * g;
        const float scoreX = 0.5f * (display.x - scoreTs.x);
        draw->AddText(promptFont, scoreSize, ImVec2(scoreX, scoreY), IM_COL32(255, 220, 80, 255), scoreBuf);
    }

    ImGui::SetCursorScreenPos(ImVec2(
        0.5f * (display.x - btnW),
        blockTop + blockH - btnH));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.22f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.08f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool exitClicked = ImGui::Button("Exit", ImVec2(btnW, btnH));
    const bool exitHovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(4);

    if (!finished && !exitClicked && seqIndex && seqCorrect && seqHit && seqDone && nSeq > 0 && cur < nSeq) {
        int pressedBoard = -1;
        for (int b = 0; b < 4; ++b) {
            const ImGuiKey k = inputMgr.GetKey(b);
            if (k == ImGuiKey_None) continue;
            const bool mouseKey = (k == ImGuiKey_MouseLeft || k == ImGuiKey_MouseRight || k == ImGuiKey_MouseMiddle);
            if (mouseKey && exitHovered) continue;
            if (ImGui::IsKeyPressed(k, false)) {
                pressedBoard = b;
                break;
            }
        }
        if (pressedBoard >= 0) {
            const bool ok = (pressedBoard == seq[cur]);
            seqHit[cur] = ok;
            if (ok) ++(*seqCorrect);
            ++(*seqIndex);
            if (*seqIndex >= nSeq) *seqDone = true;
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return exitClicked;
}

#if defined(_WIN32) || defined(_WIN64)
void SelectFolderDialog(char* targetBuffer, size_t bufferSize, std::atomic<bool>& isWaitingFlag) {
    if (isWaitingFlag.load()) return;
    isWaitingFlag.store(true);

    std::thread dialogThread([targetBuffer, bufferSize, &isWaitingFlag]() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr)) {
            IFileOpenDialog* pFileOpen = nullptr;
            hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
            if (SUCCEEDED(hr)) {
                DWORD dwOptions = 0;
                pFileOpen->GetOptions(&dwOptions);
                pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                if (SUCCEEDED(pFileOpen->Show(NULL))) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                        PWSTR pszFilePath = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                            WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1,
                                targetBuffer, static_cast<int>(bufferSize), NULL, NULL);
                            CoTaskMemFree(pszFilePath);
                        }
                        pItem->Release();
                    }
                }
                pFileOpen->Release();
            }
            CoUninitialize();
        }
        isWaitingFlag.store(false);
    });
    dialogThread.detach();
}
#endif
} // namespace

std::string UIManager::GetFullRecordPath() const {
    std::string file = recordFileName;
    if (file.empty()) file = "eeg_recording.csv";

    for (char& c : file) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    const auto dot = file.find_last_of('.');
    if (dot == std::string::npos || file.substr(dot) != ".csv") {
        file += ".csv";
    }

    std::string dir = recordDirectory;
    if (dir.empty()) return file;
    const char last = dir.back();
    if (last != '\\' && last != '/') dir += '\\';
    return dir + file;
}

void UIManager::FillDefaultRecordName() {
    std::time_t raw = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &raw);
#else
    localtime_r(&raw, &local);
#endif
    std::ostringstream ss;
    ss << std::setfill('0')
        << "eeg_"
        << std::setw(2) << (local.tm_mon + 1) << "_"
        << std::setw(2) << local.tm_mday << "_"
        << std::setw(2) << local.tm_hour << "_"
        << std::setw(2) << local.tm_min << "_"
        << std::setw(2) << local.tm_sec;
    const std::string name = ss.str();
    std::snprintf(recordFileName, sizeof(recordFileName), "%s", name.c_str());
}

void UIManager::DrawRecordConfigPopup() {
    if (!showRecordPanel) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 workPos = vp->WorkPos;
    const ImVec2 workSize = vp->WorkSize;
    const bool displayOk = workSize.x > 64.0f && workSize.y > 64.0f;
    const ImVec2 center = vp->GetCenter();

    const bool bringToFront = m_recenterRecordPanel;
    if (displayOk) {
        if (m_recenterRecordPanel) {
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
            m_recenterRecordPanel = false;
        }
        else {
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowCollapsed(false, ImGuiCond_Appearing);
        }
    }

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Record Configuration", &showRecordPanel, flags);
    ImGui::SetWindowCollapsed(false);

    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const bool offscreen = pos.x + size.x < workPos.x + 48.0f ||
        pos.y + 24.0f < workPos.y ||
        pos.x > workPos.x + workSize.x - 48.0f ||
        pos.y > workPos.y + workSize.y - 48.0f;
    if (displayOk && offscreen) {
        ImGui::SetWindowPos(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f));
    }
    if (bringToFront || ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    ImGui::Text("Enter filename:");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(280.0f * m_dpiScale);
    ImGui::InputText("##filename", recordFileName, sizeof(recordFileName));
    ImGui::SameLine();
    ImGui::TextUnformatted(".csv");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Save in:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f * m_dpiScale);
    ImGui::InputText("##directory", recordDirectory, sizeof(recordDirectory));
    ImGui::SameLine();
#if defined(_WIN32) || defined(_WIN64)
    if (isFolderDialogWaiting.load()) {
        ImGui::BeginDisabled();
        ImGui::Button("Waiting...", ImVec2(90.0f * m_dpiScale, 0));
        ImGui::EndDisabled();
    }
    else if (ImGui::Button("Browse...", ImVec2(90.0f * m_dpiScale, 0))) {
        SelectFolderDialog(recordDirectory, sizeof(recordDirectory), isFolderDialogWaiting);
    }
#else
    ImGui::TextDisabled("type path");
#endif

    ImGui::Spacing();
    ImGui::BeginDisabled(!isConnected || isRecording.load());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.70f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.80f, 0.30f, 1.0f));
    if (ImGui::Button("Start Recording", ImVec2(-1, 40.0f * m_dpiScale))) {
        reqStartRecord = true;
        showRecordPanel = false;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    if (!isConnected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Connect the board first");
    }

    ImGui::End();
}

void UIManager::DrawSignalEEGPanel() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float recBtnW = 120.0f * m_dpiScale;
    const float recBtnH = 32.0f * m_dpiScale;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - recBtnW) * 0.5f));
    if (!isRecording.load()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
        if (ImGui::Button("Record", ImVec2(recBtnW, recBtnH))) {
            FillDefaultRecordName();
            showRecordPanel = true;
            m_recenterRecordPanel = true;
        }
        ImGui::PopStyleColor(2);
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.30f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        if (ImGui::Button("Stop", ImVec2(recBtnW, recBtnH))) {
            reqStopRecord = true;
        }
        ImGui::PopStyleColor(3);
    }

    EEGPlotSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(eegPlotMutex);
        snap = eegPlot;
    }

    const float rowGap = ImGui::GetStyle().ItemSpacing.y;
    const float windowH = 5.0f;
    const int fs = snap.samplingRate > 0 ? snap.samplingRate : 200;
    const float xAxisHeight = 2.0f * ImGui::GetTextLineHeight() + 8.0f * m_dpiScale;
    float availY = ImGui::GetContentRegionAvail().y;
    float baseRowH = std::max(48.0f * m_dpiScale, (availY - 3.0f * rowGap - xAxisHeight) / 4.0f);

    const ImVec4 liveCols[4] = {
        ImVec4(0.20f, 0.62f, 0.92f, 1.0f),
        ImVec4(0.95f, 0.55f, 0.16f, 1.0f),
        ImVec4(0.28f, 0.74f, 0.42f, 1.0f),
        ImVec4(0.78f, 0.38f, 0.74f, 1.0f)
    };
    const ImVec4 greyLine = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

    for (int ch = 0; ch < 4; ++ch) {
        float rowH = (ch == 3) ? ImGui::GetContentRegionAvail().y : baseRowH;

        char rowId[32];
        snprintf(rowId, sizeof(rowId), "EEGRow%d", ch);
        ImGui::BeginChild(rowId, ImVec2(0.0f, rowH), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const int n = std::clamp(snap.count[ch], 0, kEEGPlotCapacity);
        const bool hadData = snap.hadData[ch] && n > 1;

        ImGui::BeginGroup();
        const float frameH = ImGui::GetFrameHeight();
        const float labelH = ImGui::GetTextLineHeight();
        const float groupH = frameH + labelH + ImGui::GetStyle().ItemSpacing.y;
        const float yOff = std::max(0.0f, (ImGui::GetContentRegionAvail().y - groupH) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOff);

        char checkId[32];
        snprintf(checkId, sizeof(checkId), "##signal_eeg_ch%d", ch);
        ImGui::Checkbox(checkId, &eegChannels[ch]);
        const bool on = eegChannels[ch];
        char chLabel[16];
        snprintf(chLabel, sizeof(chLabel), "Ch %d", ch + 1);
        if (on) ImGui::TextUnformatted(chLabel);
        else ImGui::TextDisabled("%s", chLabel);
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 8.0f * m_dpiScale);

        float yDisp[kEEGPlotCapacity];
        double xMin = 0.0;
        double xMax = windowH;
        double yMin = -100.0;
        double yMax = 100.0;
        if (hadData) {
            double mean = 0.0;
            for (int i = 0; i < n; ++i) mean += snap.y[ch][i];
            mean /= static_cast<double>(n);
            float lo = 0.0f;
            float hi = 0.0f;
            for (int i = 0; i < n; ++i) {
                yDisp[i] = snap.y[ch][i] - static_cast<float>(mean);
                if (i == 0 || yDisp[i] < lo) lo = yDisp[i];
                if (i == 0 || yDisp[i] > hi) hi = yDisp[i];
            }
            xMin = snap.t[ch][0];
            xMax = snap.t[ch][n - 1];
            if (xMax <= xMin) xMax = xMin + (windowH / std::max(1, fs));
            const float span = std::max(50.0f, hi - lo);
            const float pad = span * 0.12f;
            yMin = lo - pad;
            yMax = hi + pad;
        }

        const int styleColors = on ? 0 : 3;
        if (!on) {
            ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
            ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
            ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
        }

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.0f * m_dpiScale, 4.0f * m_dpiScale));
        ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(2.0f * m_dpiScale, 2.0f * m_dpiScale));

        char plotId[32];
        snprintf(plotId, sizeof(plotId), "##eeg_plot_%d", ch);
        const ImVec2 plotSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
        const ImPlotFlags flags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend |
            ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;
        const ImPlotAxisFlags xFlags = ImPlotAxisFlags_Lock |
            (ch < 3 ? (ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoLabel)
                    : ImPlotAxisFlags_None);
        const ImPlotAxisFlags yFlags = ImPlotAxisFlags_Lock;

        if (ImPlot::BeginPlot(plotId, plotSize, flags)) {
            ImPlot::SetupAxes(ch == 3 ? "Time (s)" : nullptr, "uV", xFlags, yFlags);
            ImPlot::SetupAxesLimits(xMin, xMax, yMin, yMax, ImPlotCond_Always);

            if (hadData) {
                ImPlotSpec spec;
                spec.LineColor = on ? liveCols[ch] : greyLine;
                spec.LineWeight = on ? 1.4f : 1.1f;
                spec.Flags = ImPlotItemFlags_NoLegend;
                ImPlot::PlotLine("eeg", snap.t[ch].data(), yDisp, n, spec);
            }

            if (!on) {
                ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
                ImPlot::PlotText(hadData ? "Paused" : "OFF",
                    0.5 * (xMin + xMax), 0.5 * (yMin + yMax));
                ImPlot::PopStyleColor();
            }

            ImPlot::EndPlot();
        }

        ImPlot::PopStyleVar(2);
        if (styleColors > 0) ImPlot::PopStyleColor(styleColors);

        ImGui::EndChild();
    }
}

namespace {
const char* FormatHz(float hz, char* buf, int n) {
    if (std::fabs(hz - std::round(hz)) < 0.06f) snprintf(buf, n, "%.0f", std::round(hz));
    else snprintf(buf, n, "%.2f", hz);
    return buf;
}
}

void UIManager::DrawSignalSpectrumPanel() {
    SSVEPDetectSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(eegPlotMutex);
        snap = SSVEPDetect;
    }

    char hzBuf[4][16];
    for (int i = 0; i < 4; ++i) FormatHz(snap.targetHz[i], hzBuf[i], sizeof(hzBuf[i]));

    if (!snap.ready || snap.votedWinner < 0) {
        ImGui::Text("Collecting EEG for SSVEP… %.1f / %.0f s",
            snap.secondsCollected, snap.windowSec);
    }
    else {
        ImGui::Text("Detected: %s Hz (%d/%d votes)    this window: %s Hz",
            hzBuf[snap.votedWinner], snap.votesForWinner, std::max(1, snap.votesCast),
            (snap.windowWinner >= 0) ? hzBuf[snap.windowWinner] : "-");
    }
    ImGui::TextDisabled("Gate Ratio: %.2f", snap.gateRatio);

    const float plotH = std::max(48.0f, ImGui::GetContentRegionAvail().y);
    const ImPlotFlags flags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend |
        ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;

    // 1. Determine X-axis bounds based on the actual target frequencies
    float minHz = snap.targetHz[0];
    float maxHz = snap.targetHz[0];
    for (int i = 1; i < 4; ++i) {
        if (snap.targetHz[i] < minHz) minHz = snap.targetHz[i];
        if (snap.targetHz[i] > maxHz) maxHz = snap.targetHz[i];
    }
    double plotMin = static_cast<double>(minHz) - 2.0;
    double plotMax = static_cast<double>(maxHz) + 2.0;

    // 2. Generate the smooth "Mountain" probability distribution curve
    const int curvePoints = 100;
    double xs[curvePoints];
    double ys[curvePoints];
    
    // Adjust 'sigma' to make the peaks wider (blend together) or narrower (sharp spikes)
    const double sigma = 1.2; 
    const double two_sigma_sq = 2.0 * sigma * sigma;
    double distMaxY = 0.01; // Base minimum Y to prevent axis collapsing

    for (int p = 0; p < curvePoints; ++p) {
        xs[p] = plotMin + p * (plotMax - plotMin) / (curvePoints - 1);
        double y_val = 0.0;
        
        // Sum the Gaussian distributions centered at each target frequency
        for (int i = 0; i < 4; ++i) {
            double diff = xs[p] - snap.targetHz[i];
            double weight = std::max(0.0f, snap.corr[i]); // CCA Score acts as the peak height
            y_val += weight * std::exp(-(diff * diff) / two_sigma_sq);
        }
        
        ys[p] = y_val;
        if (y_val > distMaxY) distMaxY = y_val;
    }

    if (ImPlot::BeginPlot("##SSVEP_cca_dist", ImVec2(-1.0f, plotH), flags)) {
        ImPlot::SetupAxes("Target Frequencies (Hz)", "CCA Probability", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(plotMin, plotMax, 0.0, distMaxY * 1.15, ImPlotCond_Always);
        
        // Draw the shaded area inside the mountain using ImPlotSpec (Red theme)
        ImPlotSpec fillSpec;
        fillSpec.FillColor = ImVec4(0.85f, 0.20f, 0.15f, 1.0f); // Red
        fillSpec.FillAlpha = 0.4f;                              // Transparency
        fillSpec.Flags = ImPlotItemFlags_NoLegend;
        ImPlot::PlotShaded("##cca_fill", xs, ys, curvePoints, 0.0, fillSpec);

        // Draw the solid mountain outline curve using ImPlotSpec (Red theme)
        ImPlotSpec lineSpec;
        lineSpec.LineColor = ImVec4(0.85f, 0.20f, 0.15f, 1.0f); // Red
        lineSpec.LineWeight = 2.0f;
        lineSpec.Flags = ImPlotItemFlags_NoLegend;
        ImPlot::PlotLine("cca_curve", xs, ys, curvePoints, lineSpec);

        // Draw the vertical lines identifying the actual targets
        for (int i = 0; i < 4; ++i) {
            const bool voted = (i == snap.votedWinner);
            ImPlotSpec line;
            // Bright solid red if voted winner, otherwise slightly transparent red (replacing gray)
            line.LineColor = voted ? ImVec4(1.0f, 0.1f, 0.1f, 1.0f) : ImVec4(0.85f, 0.20f, 0.15f, 0.5f);
            line.LineWeight = voted ? 2.5f : 1.0f;
            line.Flags = ImPlotItemFlags_NoLegend;
            const double x = snap.targetHz[i];
            ImPlot::PlotInfLines(hzBuf[i], &x, 1, line);
        }
        
        ImPlot::EndPlot();
    }
}

void UIManager::RenderUI() {
    int hwRefresh = GetContainingMonitorRefresh(window);
    
    // If the monitor changed, auto-calculate a VSYNC fraction that keeps rendering >= 60Hz
    if (hwRefresh != monitorRefreshHz.load()) {
        monitorRefreshHz.store(hwRefresh);
        m_swapInterval = 1;
        while (hwRefresh / (m_swapInterval + 1) >= 60) {
            m_swapInterval++;
        }
        glfwSwapInterval(m_swapInterval);
    }
    const int refresh = monitorRefreshHz.load();

    if (SSVEPTestMode) {
        signalTabActive.store(false);
        // Pass m_swapInterval as the new final argument here
        if (DrawSSVEPTestScreen(m_dpiScale, SSVEPKeyFont, SSVEPBoardLayout,
            SSVEPTestSeq, SSVEPTestSeqLen, &SSVEPTestIndex, &SSVEPTestCorrect,
            SSVEPTestHit, &SSVEPTestDone, refresh, SSVEPFrame, qrCodeTextureID, qrCodeInvTextureID)) {
            SSVEPTestMode = false;
            LeaveSSVEPTestFullscreen(window, m_preTestX, m_preTestY, m_preTestW, m_preTestH, m_swapInterval);
        }
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin("Main Dashboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    signalTabActive.store(false);

    float totalWidth = ImGui::GetContentRegionAvail().x;

    // ==========================================
    // HEADER
    // ==========================================
    if (fonts.find(32) != fonts.end()) ImGui::PushFont(fonts[32]);
    ImGui::Text("Ganglion SSVEP");
    if (fonts.find(32) != fonts.end()) ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();

    float leftColumnWidth = totalWidth * 0.25f;
    const ImGuiWindowFlags panelScrollFlags = ImGuiWindowFlags_HorizontalScrollbar;

    // ==========================================
    // LEFT PANEL: CONNECTION & GLOBAL SETTINGS
    // ==========================================
    ImGui::BeginChild("ControlPanel", ImVec2(leftColumnWidth, 0), true, panelScrollFlags);

    if (fonts.find(24) != fonts.end()) ImGui::PushFont(fonts[24]);
    ImGui::Text("Device Connection");
    if (fonts.find(24) != fonts.end()) ImGui::PopFont();
    ImGui::Spacing();

    ImGui::Text("MAC");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Enter the device MAC address");
        ImGui::Spacing();
        if (macHelpTextureID != 0) {
            ImVec2 tooltipImageSize = ImVec2(250.0f * m_dpiScale, 241.0f * m_dpiScale);
            ImGui::Image((void*)(intptr_t)macHelpTextureID, tooltipImageSize);
        }
        ImGui::EndTooltip();
    }

    ImGui::SameLine();

    float buttonWidth = 50.0f * m_dpiScale;
    float spaceBetween = ImGui::GetStyle().ItemSpacing.x;

    float rightEdgeX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    float buttonStartX = rightEdgeX - buttonWidth;

    float currentX = ImGui::GetCursorPosX();
    float availableForInputsAndColons = buttonStartX - spaceBetween - currentX;

    float colonWidth = ImGui::CalcTextSize(":").x * m_dpiScale;
    float totalColonsWidth = 5.0f * (colonWidth);

    float singleInputWidth = (availableForInputsAndColons - totalColonsWidth) / 6.0f;

    static int nextFocusTarget = -1;
    for (int i = 0; i < 6; ++i) {
        if (i > 0) {
            ImGui::SameLine(0, 1.0f);
            ImGui::Text(":");
            ImGui::SameLine(0, 1.0f);
        }

        ImGui::SetNextItemWidth(singleInputWidth);
        ImGui::PushID(i);

        if (nextFocusTarget == i) {
            ImGui::SetKeyboardFocusHere(0);
            nextFocusTarget = -1;
        }

        bool edited = ImGui::InputText("##mac", macParts[i], 3, ImGuiInputTextFlags_CharsHexadecimal);

        if (edited && strlen(macParts[i]) == 2 && i < 5) {
            nextFocusTarget = i + 1;
        }

        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && i > 0) {
            if (strlen(macParts[i]) == 0 && !edited) {
                nextFocusTarget = i - 1;
            }
        }

        ImGui::PopID();
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(buttonStartX);
    if (ImGui::Button("Clear", ImVec2(buttonWidth, 0))) {
        for (int i = 0; i < 6; ++i) {
            macParts[i][0] = '\0';
        }
        nextFocusTarget = 0;
    }

    snprintf(macAddress, sizeof(macAddress), "%s:%s:%s:%s:%s:%s",
        macParts[0], macParts[1], macParts[2], macParts[3], macParts[4], macParts[5]);

    ImGui::Text("Port");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::Combo("##Port_Combo", &selectedPortIdx, ProPortVectorGetter, &availablePorts, static_cast<int>(availablePorts.size()));

    ImGui::SameLine();
    if (ImGui::Button("Refresh##Refresh", ImVec2(buttonWidth, 0))) {
        UpdateAvailablePorts();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rescan system serial channels");

    ImGui::Spacing();

    // Connect Button
    if (isConnected) {
        if (ImGui::Button("Disconnect", ImVec2(-1, 40))) {
            reqDisconnect = true;
        }
    }
    else {
        ImGui::BeginDisabled(isCheckingImpedance);
        if (ImGui::Button("Connect", ImVec2(-1, 40))) {
            reqConnect = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && isCheckingImpedance) ImGui::SetTooltip("Stop checking impedance first");
        ImGui::EndDisabled();
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // ==========================================
    // RIGHT PANEL: TABS & DATA
    // ==========================================
    ImGui::BeginChild("DataPanel", ImVec2(0, 0), true, panelScrollFlags);

    if (ImGui::BeginTabBar("DataTabs")) {

        // ------------------------------------------
        // TAB: SSVEP
        // ------------------------------------------
        if (ImGui::BeginTabItem("SSVEP")) {
            if (ImGui::BeginTabBar("SSVEPSubTabs")) {

                // ------------------------------------------
                // SUBTAB: Signal (default)
                // ------------------------------------------
                if (ImGui::BeginTabItem("Signal")) {
                    signalTabActive.store(true);
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    const float spacing = ImGui::GetStyle().ItemSpacing.y;
                    const float topH = std::max(0.0f, (avail.y - spacing) * 0.60f);

                    ImGui::BeginChild("SSVEPSignalTopPanel", ImVec2(0.0f, topH), true, panelScrollFlags);
                    DrawSignalEEGPanel();
                    ImGui::EndChild();

                    ImGui::BeginChild("SSVEPSignalBottomPanel", ImVec2(0.0f, 0.0f), true, panelScrollFlags);
                    DrawSignalSpectrumPanel();
                    ImGui::EndChild();

                    ImGui::EndTabItem();
                }

                // ------------------------------------------
                // SUBTAB: HUD (existing SSVEP controls)
                // ------------------------------------------
                if (ImGui::BeginTabItem("HUD")) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const float spacing = ImGui::GetStyle().ItemSpacing.y;
            const float bottomH = avail.y * 0.10f;
            const float topH = std::max(0.0f, avail.y - bottomH - spacing);

            ImGui::BeginChild("SSVEPTopPanel", ImVec2(0.0f, topH), true, ImGuiWindowFlags_HorizontalScrollbar);
            {
                const char* patternItems[] = { "Plain Board", "QR Code" };
                ImGui::SetNextItemWidth(150.0f * m_dpiScale);
                if (ImGui::Combo("Pattern", &SSVEPPatternMode, patternItems, IM_ARRAYSIZE(patternItems))) {
                    SSVEPBoardLayout.patternMode = SSVEPPatternMode;
                }
                
                if (SSVEPPatternMode == 1) {
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Contrast Mode", &SSVEPContrastMode)) {
                        SSVEPBoardLayout.contrastMode = SSVEPContrastMode;
                    }
                }

                ImGui::SameLine();
                if (ImGui::Checkbox("Smooth Flicker", &SSVEPSmoothMode)) {
                    SSVEPBoardLayout.smoothMode = SSVEPSmoothMode;
                }

                ImGui::Spacing();

                ImVec2 content = ImGui::GetContentRegionAvail();
                const float toggleH = 28.0f * m_dpiScale;
                const float toggleW = 52.0f * m_dpiScale;
                const float rowPad = 8.0f * m_dpiScale;
                const float rowH = toggleH + rowPad;

                ImVec2 previewOrigin = ImGui::GetCursorScreenPos();
                ImVec2 previewSize = ImVec2(content.x, std::max(0.0f, content.y - rowH));

                float monitorW = 1920.0f;
                float monitorH = 1080.0f;
                GetContainingMonitorSize(window, &monitorW, &monitorH);

                DrawSSVEPPreviewBoards(previewOrigin, previewSize, SSVEPPreviewEnabled, m_dpiScale, SSVEPKeyFont,
                    &SSVEPBoardLayout, &SSVEPBoardLayoutInit, monitorW, monitorH, refresh, SSVEPFrame, qrCodeTextureID, qrCodeInvTextureID);

                ImGui::Dummy(previewSize);

                if (hudOverlay.IsActive()) {
                    hudOverlay.Sync(SSVEPBoardLayout);
                }

                const char* hideAllLabel = "Hide All";
                const char* previewLabel = "Preview";
                const char* resetLabel = "Reset";
                const float labelW = ImGui::CalcTextSize(previewLabel).x;

                const float gap = 8.0f * m_dpiScale;
                const ImVec2 hideAllSize(ImGui::CalcTextSize(hideAllLabel).x + 16.0f * m_dpiScale, toggleH);
                const ImVec2 resetSize(
                    ImGui::CalcTextSize(resetLabel).x + 16.0f * m_dpiScale,
                    toggleH);
                    const float sliderW = 120.0f * m_dpiScale;

                const float groupW = hideAllSize.x + gap + sliderW + gap + labelW + gap + toggleW + gap + resetSize.x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - groupW) * 0.5f));

                if (ImGui::Button(hideAllLabel, hideAllSize)) {
                    for (int i = 0; i < 4; ++i) {
                        SSVEPBoardLayout.visible[i] = false;
                    }
                }
                ImGui::SameLine(0.0f, gap);

                ImGui::SetNextItemWidth(sliderW);
                ImGui::SliderFloat("##WindowSlider", &SSVEPWindowDuration, 1.0f, 10.0f, "%.1f s");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("SSVEP Window & Step Duration");
                ImGui::SameLine(0.0f, gap);

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(previewLabel);
                ImGui::SameLine(0.0f, gap);
                DrawSlideToggle("##SSVEPPreviewToggle", &SSVEPPreviewEnabled, m_dpiScale);
                ImGui::SameLine(0.0f, gap);
                if (ImGui::Button(resetLabel, resetSize)) {
                    // 1. Reset Positions (triggers recalculation in DrawSSVEPPreviewBoards)
                    SSVEPBoardLayoutInit = false; 
                    
                    // 2. Reset Box Sizes to default (0.10f)
                    for (int i = 0; i < 4; ++i) {
                        SSVEPBoardLayout.sizeFrac[i] = 0.10f;
                    }
                    
                    // 3. Reset Frequencies to defaults
                    SSVEPBoardLayout.customHz[0] = 5.0f;
                    SSVEPBoardLayout.customHz[1] = 9.0f;
                    SSVEPBoardLayout.customHz[2] = 13.0f;
                    SSVEPBoardLayout.customHz[3] = 17.0f;
                    
                    // 4. Reset Keybinds using our new InputManager method
                    InputManager::GetInstance().ResetAllKeys();
                }
            }
            ImGui::EndChild();

            ImGui::BeginChild("SSVEPBottomPanel", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            {
                const float gap = ImGui::GetStyle().ItemSpacing.x;
                ImVec2 btnArea = ImGui::GetContentRegionAvail();
                const float btnW = std::max(1.0f, (btnArea.x - gap) * 0.5f);
                const float btnH = std::max(1.0f, btnArea.y);

                if (ImGui::Button("Test", ImVec2(btnW, btnH))) {
                    if (hudOverlay.IsActive()) {
                        hudOverlay.Leave();
                    }
                    EnterSSVEPTestFullscreen(window, &m_preTestX, &m_preTestY, &m_preTestW, &m_preTestH);
                    GenerateSSVEPTestSequence(SSVEPTestSeq, &SSVEPTestSeqLen, SSVEPBoardLayout.visible);
                    SSVEPTestIndex = 0;
                    SSVEPTestCorrect = 0;
                    SSVEPTestDone = false;
                    for (int i = 0; i < 12; ++i) SSVEPTestHit[i] = false;
                    SSVEPTestMode = true;
                }
                ImGui::SameLine();

                if (hudOverlay.IsActive()) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.12f, 0.12f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.22f, 0.22f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.08f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    if (ImGui::Button("Exit HUD Mode", ImVec2(btnW, btnH))) {
                        hudOverlay.Leave();
                    }
                    ImGui::PopStyleColor(4);
                }
                else if (ImGui::Button("HUD Mode", ImVec2(btnW, btnH))) {
                    hudOverlay.Enter(window, SSVEPBoardLayout);
                }
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::EndTabItem();
        }

        // ------------------------------------------
        // TAB: IMPEDANCE CHECK
        // ------------------------------------------
        if (ImGui::BeginTabItem("Impedance Check")) {
            ImGui::Spacing();

            if (isCheckingImpedance) {
                if (ImGui::Button("Stop Checking", ImVec2(-1, 40))) {
                    reqStopImpedanceCheck = true;
                }
            }
            else {
                ImGui::BeginDisabled(isConnected);
                if (ImGui::Button("Check Impedance", ImVec2(-1, 40))) {
                    reqImpedanceCheck = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && isConnected) ImGui::SetTooltip("Disconnect the board first");
                ImGui::EndDisabled();
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // Calculate widths BEFORE initializing columns to keep layout boundaries stable
            float leftColumnWidth = ImGui::GetContentRegionAvail().x * 0.60f;

            // Initialize 2-Column Layout
            ImGui::Columns(2, "ImpedanceColumns", false);
            ImGui::SetColumnWidth(0, leftColumnWidth);

            // ===================================
            // COLUMN 1: 3D HEAD VIEWPORT
            // ===================================
            ImGui::BeginChild("HeadViewPanel", ImVec2(0, 0), true);

            ImVec2 viewportSize = ImGui::GetContentRegionAvail();
            headObject.Draw();

            headObject.UpdateSubObjectScreenPositions(viewportSize, ImGui::GetItemRectMin());

            struct PanelData {
                int id;
                bool isNew;
                ImVec2 startPos;
            };
            static std::vector<PanelData> openPanels;

            // NEW: Global focus restoration trigger flag
            static bool pushPanelsToFront = false;

            if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                float closestDistance = viewportSize.x * 0.05f;
                int clickedID = -1;

                for (const auto& obj : headObject.subObjects) {
                    if (!obj.isSelectable) continue;

                    float dx = mousePos.x - obj.screenPos.x;
                    float dy = mousePos.y - obj.screenPos.y;
                    float dist = sqrt(dx * dx + dy * dy);

                    if (dist < closestDistance) {
                        closestDistance = dist;
                        clickedID = obj.id;
                    }
                }

                if (clickedID != -1) {
                    headObject.selectedObjectID = clickedID;

                    bool alreadyOpen = false;
                    for (const auto& panel : openPanels) {
                        if (panel.id == clickedID) {
                            alreadyOpen = true;
                            break;
                        }
                    }

                    if (!alreadyOpen) {
                        openPanels.push_back({ clickedID, true, ImVec2(mousePos.x - 15, mousePos.y - 15) });
                    }
                }

                // Whenever a click registers in the viewport, tell all panels to pop back to the top
                pushPanelsToFront = true;
            }

            ImGui::EndChild();
            ImGui::NextColumn();

            // ===================================
            // COLUMN 2: IMPEDANCE VALUE LIST
            // ===================================
            ImGui::BeginChild("ImpedanceListPanel", ImVec2(0, 0), true);
            ImGui::Text("Electrode Status");
            ImGui::Separator();
            ImGui::Spacing();

            const char* chanNames[5] = { "1", "2", "3", "4", "REF" };
            for (int i = 0; i < 5; i++) {

                // 1. Check if the current channel is active
                bool isChannelOn = true;
                if (i < 4) {
                    // Unique hidden labels (##) keep the text invisible but functional
                    char checkLabel[32];
                    snprintf(checkLabel, sizeof(checkLabel), "##impedance_toggle_%d", i);

                    ImGui::Checkbox(checkLabel, &eegChannels[i]);
                    isChannelOn = eegChannels[i];
                    ImGui::SameLine();
                }
                else {
                    // Padding block for REF so its text and circle align perfectly with channels 1-4
                    float checkWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
                    ImGui::Dummy(ImVec2(checkWidth, 0));
                    ImGui::SameLine();
                }

                // 2. Determine circle color based on ON/OFF state and impedance values
                ImU32 col;
                if (!isChannelOn) {
                    col = IM_COL32(128, 128, 128, 255); // Neutral Gray for deactivated channels
                }
                else {
                    col = IM_COL32(224, 56, 45, 255); // Default Red
                    if (currentImpedances[i] <= 10)      col = IM_COL32(49, 113, 89, 255);
                    else if (currentImpedances[i] <= 50) col = IM_COL32(184, 220, 105, 255);
                    else if (currentImpedances[i] <= 100)col = IM_COL32(221, 178, 13, 255);
                    else if (currentImpedances[i] <= 150)col = IM_COL32(253, 94, 52, 255);
                }

                // 3. Draw circle indicator centered vertically with the text line
                ImVec2 p = ImGui::GetCursorScreenPos();
                float verticalCenter = ImGui::GetFrameHeight() * 0.5f;
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + (10 * m_dpiScale), p.y + verticalCenter), 8.0f * m_dpiScale, col);

                ImGui::Dummy(ImVec2(25 * m_dpiScale, ImGui::GetFrameHeight()));
                ImGui::SameLine();

                // 4. Update the label conditionally
                if (!isChannelOn) {
                    ImGui::Text("Channel %s: OFF", chanNames[i]);
                }
                else {
                    ImGui::Text("Channel %s: %.1f kΩ", chanNames[i], currentImpedances[i]);
                }

                ImGui::Spacing(); ImGui::Spacing();
            }
            ImGui::EndChild();

            // ===================================
            // FLOATING COMPONENT INFO WINDOWS
            // ===================================
            for (auto it = openPanels.begin(); it != openPanels.end(); ) {
                bool keepOpen = true;

                const SubObject* targetObj = nullptr;
                for (const auto& obj : headObject.subObjects) {
                    if (obj.id == it->id) {
                        targetObj = &obj;
                        break;
                    }
                }

                if (targetObj) {
                    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysVerticalScrollbar;

                    ImGuiViewport* viewport = ImGui::GetMainViewport();

                    // 1. Calculate Width (e.g., 12.5% of total screen width)
                    float responsiveWidth = viewport->WorkSize.x * 0.125f;
                    if (responsiveWidth < 220.0f) responsiveWidth = 220.0f;

                    // NEW: 2. Calculate Height (e.g., 35% of total screen height)
                    float responsiveHeight = viewport->WorkSize.y * 0.35f;
                    if (responsiveHeight < 300.0f) responsiveHeight = 300.0f;

                    // 3. Apply the limits on the very first frame
                    if (it->isNew) {
                        ImGui::SetNextWindowPos(it->startPos, ImGuiCond_Always);

                        // Set the exact initial dimensions based on our responsive math
                        ImGui::SetNextWindowSize(ImVec2(responsiveWidth, responsiveHeight), ImGuiCond_Always);

                        it->isNew = false;
                    }

                    std::string windowLabel = targetObj->name + "###CompWindow_" + std::to_string(targetObj->id);

                    ImGui::Begin(windowLabel.c_str(), &keepOpen, windowFlags);

                    if (pushPanelsToFront) {
                        ImGui::SetWindowFocus();
                    }
                    if (!ImGui::IsWindowCollapsed()) {

                        float currentWidth = ImGui::GetWindowWidth();
                        float fontScale = currentWidth / responsiveWidth;

                        if (fontScale < 1.0f) fontScale = 1.0f;
                        if (fontScale > 2.0f) fontScale = 2.0f;
                        ImGui::SetWindowFontScale(fontScale);

                        // ==================================================
                        // 1. LIVE HARDWARE CONTROL & IMPEDANCE MAPPING (FIRST)
                        // ==================================================
                        // Map the 3D model node names to their corresponding array indices
                        int chanIdx = -1;
                        if (targetObj->name == "Fp1")      chanIdx = 0;
                        else if (targetObj->name == "Fp2") chanIdx = 1;
                        else if (targetObj->name == "T3")  chanIdx = 2;
                        else if (targetObj->name == "T4")  chanIdx = 3;
                        else if (targetObj->name == "A2")  chanIdx = 4;

                        if (chanIdx != -1) {
                            if (chanIdx != 4) {
                                // 1. Interactive Checkbox (Synced with all other windows)

                                char checkId[64];
                                snprintf(checkId, sizeof(checkId), "##float_check_%d", chanIdx);

                                ImGui::Checkbox(checkId, &eegChannels[chanIdx]);
                                ImGui::SameLine();
                                ImGui::Text("|");
                                ImGui::SameLine();
                            }

                            // 2. Conditional Impedance Status Text & Color Matching
                            
                            if (chanIdx == 4 || eegChannels[chanIdx]) {
                                // Channel is active, match the exact color spectrum from your list panel
                                float imp = currentImpedances[chanIdx];
                                ImVec4 statusColor = ImVec4(224.0f / 255.0f, 56.0f / 255.0f, 45.0f / 255.0f, 1.0f);

                                if (imp <= 10)       statusColor = ImVec4(49.0f / 255.0f, 113.0f / 255.0f, 89.0f / 255.0f, 1.0f);
                                else if (imp <= 50)  statusColor = ImVec4(184.0f / 255.0f, 220.0f / 255.0f, 105.0f / 255.0f, 1.0f);
                                else if (imp <= 100) statusColor = ImVec4(221.0f / 255.0f, 178.0f / 255.0f, 13.0f / 255.0f, 1.0f);
                                else if (imp <= 150) statusColor = ImVec4(253.0f / 255.0f, 94.0f / 255.0f, 52.0f / 255.0f, 1.0f);

                                ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "Impedance:");
                                ImGui::SameLine();
                                ImGui::TextColored(statusColor, "%.1f kΩ", imp);
                            }
                            else {
                                // Channel is deactivated
                                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Status: OFF");
                            }

                            // Clear separator to divide the hardware row from the image below
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();
                        }

                        // ==================================================
                        // 2. IMAGE RATIO PRESERVATION (SECOND)
                        // ==================================================
                        if (targetObj->textureID != 0) {
                            // 1. Get exact available horizontal width in the user's resized panel
                            float availWidth = ImGui::GetContentRegionAvail().x;
                            float aspectRatio = 0.83f;

                            // 3. Math ensures height perfectly matches the width context natively
                            ImVec2 dynamicDisplaySize = ImVec2(availWidth, availWidth * aspectRatio);

                            ImGui::Image((void*)(intptr_t)targetObj->textureID, dynamicDisplaySize);

                            // Clear separator to divide the image from the description below
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();
                        }

                        // ==================================================
                        // 3. DESCRIPTION (THIRD)
                        // ==================================================
                        ImGui::TextWrapped("%s", targetObj->description.c_str());

                        ImGui::SetWindowFontScale(1.0f); // Reset
                    }

                    ImGui::End();
                }

                if (!keepOpen) {
                    if (headObject.selectedObjectID == it->id) {
                        headObject.selectedObjectID = -1;
                    }
                    it = openPanels.erase(it);
                }
                else {
                    ++it;
                }
            }

            if (pushPanelsToFront) {
                pushPanelsToFront = false;
            }

            ImGui::Columns(1);
            ImGui::EndTabItem();
        }

        // ------------------------------------------
        // TAB 2: BAND POWER
        // ------------------------------------------
        if (ImGui::BeginTabItem("Band Power")) {
            ImGui::Spacing();

            const char* waves[] = { "Delta", "Theta", "Alpha", "Beta", "Gamma" };
            for (int i = 0; i < 5; i++) {
                ImGui::Checkbox(waves[i], &selectedWaves[i]);
                if (i < 4) ImGui::SameLine();
            }
            ImGui::Spacing();

            // Setup Bar Chart Layout (Top Half)
            if (ImPlot::BeginPlot("Current Band Powers", ImVec2(-1, ImGui::GetContentRegionAvail().y * 0.5f))) {
                ImPlot::SetupAxes("Brainwave Type", "Band Power", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
                ImPlot::SetupAxesLimits(-0.5, 4.5, 0, 1, ImPlotCond_Always);
                
                // Colors for bars: blue, cyan, green, orange, red
                ImU32 barColors[5] = {
                    IM_COL32(0,0,255,255),     // Delta (Blue)
                    IM_COL32(0,255,255,255),   // Theta (Cyan)
                    IM_COL32(0,255,0,255),     // Alpha (Green)
                    IM_COL32(255,165,0,255),   // Beta (Orange)
                    IM_COL32(255,0,0,255)      // Gamma (Red)
                 };

                for (int i = 0; i < 5; i++) {
                    if (selectedWaves[i]) {
                        double x = i;
                        double y = currentBandPowers[i];
                        //std::cout << y << " ";

                        ImVec4 colorVec = ColorConvertU32ToFloat4(barColors[i]);
                        ImPlotSpec spec;
                        spec.FillColor = colorVec;
                        spec.FillAlpha = 1.0f;
                        spec.LineColor = colorVec;
                        spec.LineWeight = 1.0f;

                        ImPlot::PlotBars(waves[i], &x, &y, 1, 0.5, spec);
                    }
                }
                ImPlot::EndPlot();
                //std::cout << "\n";
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // 1. Check if main.cpp told us to show the popup
    if (showPortErrorPopup) {
        ImGui::OpenPopup("Connection Error");
        showPortErrorPopup = false; // Reset the trigger
    }

    // 2. Render the Modal Box (Locks the background until user clicks OK)
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Connection Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        // Warning Icon and text
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "CRITICAL CONNECTION FAILURE");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("%s", portErrorMessage.c_str());
        ImGui::Spacing();
        ImGui::Text("Please ensure the port is correct and not used by another application.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Centered OK Button
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 120) * 0.5f);
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showRecordErrorPopup) {
        ImGui::OpenPopup("Record Error");
        showRecordErrorPopup = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Record Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Could not start recording");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted(recordErrorMessage.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 120) * 0.5f);
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();

    DrawRecordConfigPopup();
}

ImVec4 UIManager::ColorConvertU32ToFloat4(ImU32 in) {
    float s = 1.0f / 255.0f;
    return ImVec4(
        ((in >> IM_COL32_R_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_G_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_B_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_A_SHIFT) & 0xFF) * s);
}

void UIManager::Cleanup() {
    hudOverlay.Leave();
    if (window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        window = nullptr;
    }
}
