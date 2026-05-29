#include <glad/glad.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif
#include <setupapi.h>
#include <devguid.h>
#include "UI.h"
#include "implot.h"
#include "Utils.h"
#include <cctype>

#pragma comment(lib, "setupapi.lib")

#define SAMPLING_RATE 200.0

void UIManager::UpdateAvailablePorts() {
    availablePorts.clear();

#if defined(_WIN32) || defined(_WIN64)
    // ==================================================
    // --- WINDOWS HARDWARE SETUPAPI FILTERING ---
    // ==================================================
    // Query Windows for devices belonging to the "Ports" (COM & LPT) setup class
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
    window = glfwCreateWindow(1280, 720, "BrainFlow Dashboard", nullptr, nullptr);
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 4. Load Fonts
    const char* fontPath = "../assets/segoeui.ttf";
    std::vector<int> desiredSizes = { 16, 20, 24, 32, 40, 48, 60 };

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 3;

    for (int size : desiredSizes) {
        ImFont* loadedFont = io.Fonts->AddFontFromFileTTF(fontPath, (float)size, &fontConfig);
        if (loadedFont != nullptr) {
            fonts[size] = loadedFont;
        }
    }
    if (fonts.empty()) {
        io.Fonts->AddFontDefault(); // Fallback
    }

    // 5. Apply Theme
    SetupCustomTheme();
    UpdateAvailablePorts();

    headObject.InitModel("../assets/head_object.obj", "../assets");
    macHelpTextureID = LoadComponentTexture("../assets/mac_help.jpeg");

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
    // Main render loop
    while (!glfwWindowShouldClose(window) && isRunning) {
        glfwPollEvents();

        // Start new ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Draw your actual dashboard
        RenderUI();

        // Render
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
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

void UIManager::RenderUI() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin("Main Dashboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // ==========================================
    // HEADER
    // ==========================================
    if (fonts.find(32) != fonts.end()) ImGui::PushFont(fonts[32]);
    ImGui::Text("Ganglion Control Center");
    if (fonts.find(32) != fonts.end()) ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(2, "MainColumns", false);
    ImGui::SetColumnWidth(0, 350.0f); // Left Panel Width

    // ==========================================
    // LEFT PANEL: CONNECTION & GLOBAL SETTINGS
    // ==========================================
    ImGui::BeginChild("ControlPanel", ImVec2(0, 0), true);

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
            ImVec2 tooltipImageSize = ImVec2(250.0f, 150.0f);
            ImGui::Image((void*)(intptr_t)macHelpTextureID, tooltipImageSize);
        }
        ImGui::EndTooltip();
    }

    ImGui::SameLine();

    static int nextFocusTarget = -1;

    for (int i = 0; i < 6; ++i) {
        if (i > 0) {
            ImGui::SameLine(0, 4.0f);
            ImGui::Text(":");
            ImGui::SameLine(0, 4.0f);
        }

        ImGui::SetNextItemWidth(25.0f);
        ImGui::PushID(i);

        if (nextFocusTarget == i) {
            ImGui::SetKeyboardFocusHere(0);
            nextFocusTarget = -1;
        }

        bool edited = ImGui::InputText("", macParts[i], 3, ImGuiInputTextFlags_CharsHexadecimal);

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

    ImGui::SameLine(0, 8.0f);
    if (ImGui::Button("Clear")) {
        for (int i = 0; i < 6; ++i) {
            macParts[i][0] = '\0';
        }
        nextFocusTarget = 0;
    }

    snprintf(macAddress, sizeof(macAddress), "%s:%s:%s:%s:%s:%s",
        macParts[0], macParts[1], macParts[2], macParts[3], macParts[4], macParts[5]);

    ImGui::Text("Port");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 45.0f);
    ImGui::Combo("##Port_Combo", &selectedPortIdx, ProPortVectorGetter, &availablePorts, static_cast<int>(availablePorts.size()));

    ImGui::SameLine();
    if (ImGui::Button("##Refresh", ImVec2(35, 0))) {
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
        if (ImGui::Button("Connect", ImVec2(-1, 40))) {
            reqConnect = true;
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::Text("EEG Channels:");
    ImGui::Checkbox("1", &eegChannels[0]); ImGui::SameLine();
    ImGui::Checkbox("2", &eegChannels[1]); ImGui::SameLine();
    ImGui::Checkbox("3", &eegChannels[2]); ImGui::SameLine();
    ImGui::Checkbox("4", &eegChannels[3]);

    ImGui::Spacing();
    const char* inputModes[] = { "Press", "Hold" };
    ImGui::Combo("Input Mode", &inputModeIdx, inputModes, IM_ARRAYSIZE(inputModes));

    ImGui::Spacing();
    ImGui::Text("Input 1 (Relax): %s", input1Text.c_str()); ImGui::SameLine(250);
    if (ImGui::Button("Set##1")) { /* Logic to await keypress */ }

    ImGui::Text("Input 2 (Focus): %s", input2Text.c_str()); ImGui::SameLine(250);
    if (ImGui::Button("Set##2")) { /* Logic to await keypress */ }

    ImGui::Spacing();
    ImGui::SliderFloat("Focus Threshold", &focusThreshold, 0.1f, 0.9f);
    ImGui::SliderFloat("Refresh Time (s)", &statusRefreshTime, 0.1f, 4.0f);

    ImGui::EndChild();
    ImGui::NextColumn();

    // ==========================================
    // RIGHT PANEL: TABS & DATA
    // ==========================================
    ImGui::BeginChild("DataPanel", ImVec2(0, 0), true);

    if (ImGui::BeginTabBar("DataTabs")) {

        // ------------------------------------------
        // TAB 1: CONTROL (Concentration Status)
        // ------------------------------------------
        if (ImGui::BeginTabItem("Control")) {
            ImGui::Spacing();

            // Centered Status Display
            ImGui::Text("Concentration Level: %.2f", currentConcentration);

            // Draw Status Circle
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImU32 statusColor = (currentConcentration >= focusThreshold) ? IM_COL32(34, 90, 8, 255) : IM_COL32(255, 0, 0, 255);
            draw_list->AddCircleFilled(ImVec2(p.x + 20, p.y + 20), 15.0f, statusColor);
            ImGui::SetCursorScreenPos(ImVec2(p.x + 50, p.y));
            ImGui::Text("\nStatus: %s", (currentConcentration >= focusThreshold) ? "Focusing" : "Relaxing");
            ImGui::Spacing(); ImGui::Spacing();

            // Concentration Line Plot
            if (ImPlot::BeginPlot("Concentration Level", ImVec2(-1, -1))) {
                // 1. Change the axis label to "Time (s)" and lock it completely
                ImPlot::SetupAxes("Time (s)", "Concentration Level", ImPlotAxisFlags_Lock, ImPlotAxisFlags_Lock);

                double xMax = (concentrationHistory.TotalPoints < (SAMPLING_RATE * xRange)) ? SAMPLING_RATE * xRange : (double)concentrationHistory.TotalPoints;
                double xMin = (xMax < SAMPLING_RATE * xRange) ? 0.0 : xMax - (SAMPLING_RATE * xRange);
                xMax = xMax / SAMPLING_RATE;
                xMin = xMin / SAMPLING_RATE;

                ImPlot::SetupAxesLimits(xMin, xMax, 0.0, 1.0, ImPlotCond_Always);

                // 2. Unpack and convert sample indices to static timestamps
                int currentSize = (int)concentrationHistory.Data.size();
                if (currentSize > 0) {
                    std::vector<double> histX(currentSize);
                    std::vector<double> histY(currentSize);

                    double x0 = (double)(concentrationHistory.TotalPoints - currentSize);

                    for (int i = 0; i < currentSize; ++i) {
                        // Unwraps the ring buffer starting from the oldest point
                        int ringIdx = (concentrationHistory.Offset + i) % currentSize;

                        histX[i] = (x0 + i) / SAMPLING_RATE;
                        histY[i] = (double)concentrationHistory.Data[ringIdx];
                    }

                    ImPlotSpec historySpec;
                    historySpec.LineColor = ImVec4(0.0f, 0.5f, 1.0f, 1.0f); // Blue history line
                    historySpec.LineWeight = 2.0f;

                    ImPlot::PlotLine("Level", histX.data(), histY.data(), currentSize, historySpec);
                }

                // 3. Stretch the red threshold line perfectly across our static 0.0 to 1.0s window
                double threshLineX[2] = { xMin, xMax };
                double threshLineY[2] = { (double)focusThreshold, (double)focusThreshold };

                ImPlotSpec thresholdSpec;
                thresholdSpec.LineColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                thresholdSpec.LineWeight = 2.0f;
                ImPlot::PlotLine("Threshold Target", threshLineX, threshLineY, 2, thresholdSpec);

                ImPlot::EndPlot();
            }
            ImGui::EndTabItem();
        }

        // ------------------------------------------
        // TAB 2: IMPEDANCE CHECK
        // ------------------------------------------
        if (ImGui::BeginTabItem("Impedance Check")) {
            ImGui::Spacing();

            // Top Control Button
            if (ImGui::Button(isCheckingImpedance ? "Stop Checking" : "Check Impedance", ImVec2(200, 40))) {
                isCheckingImpedance = !isCheckingImpedance;
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            // Calculate widths BEFORE initializing columns to keep layout boundaries stable
            float totalWidth = ImGui::GetContentRegionAvail().x;
            float leftColumnWidth = totalWidth * 0.60f;

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
                float closestDistance = viewportSize.x * 0.02f;
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
            // CHANGED: Set height to 0 here as well to match the left column
            ImGui::BeginChild("ImpedanceListPanel", ImVec2(0, 0), true);
            ImGui::Text("Electrode Status");
            ImGui::Separator();
            ImGui::Spacing();

            const char* chanNames[5] = { "1", "2", "3", "4", "REF" };
            for (int i = 0; i < 5; i++) {
                // Determine color based on impedance
                ImU32 col = IM_COL32(255, 0, 0, 255); // Default Red
                if (impedanceValues[i] <= 20)      col = IM_COL32(34, 90, 8, 255);
                else if (impedanceValues[i] <= 50) col = IM_COL32(0, 255, 0, 255);
                else if (impedanceValues[i] <= 100)col = IM_COL32(255, 255, 0, 255);
                else if (impedanceValues[i] <= 200)col = IM_COL32(255, 165, 0, 255);

                // Draw circle indicator
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 10, p.y + 10), 8.0f, col);

                ImGui::SetCursorScreenPos(ImVec2(p.x + 30, p.y));
                ImGui::Text("Channel %s: %.1f kΩ", chanNames[i], impedanceValues[i]);
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

                        // Title Text
                        ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "Component Node: %d", targetObj->id);
                        ImGui::Separator();
                        ImGui::Spacing();

                        // IMAGE RATIO PRESERVATION
                        if (targetObj->textureID != 0) {
                            // 1. Get exact available horizontal width in the user's resized panel
                            float availWidth = ImGui::GetContentRegionAvail().x;
                            float aspectRatio = 0.83f;

                            // 3. Math ensures height perfectly matches the width context natively
                            ImVec2 dynamicDisplaySize = ImVec2(availWidth, availWidth * aspectRatio);

                            ImGui::Image((void*)(intptr_t)targetObj->textureID, dynamicDisplaySize);
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::Spacing();
                        }

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
        // TAB 3: BAND POWER
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
                ImPlot::SetupAxes("Brainwave Type", "Log Band Power", ImPlotAxisFlags_AutoFit, ImPlotScale_Log10);
                ImPlot::SetupAxesLimits(-0.5, 4.5, 0.1, 100, ImPlotCond_Always);

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

                        // 1. Push the target colors onto the modern ImPlot style stack
                        // ImPlotCol_Fill changes the internal body of the bar
                        ImPlot::PushStyleColor(i, barColors[i]);

                        // ImPlotCol_Line changes the outer stroke/border line of the bar
                        ImPlot::PushStyleColor(i, barColors[i]);

                        // 2. Render the individual bar element
                        ImPlot::PlotBars(waves[i], &x, &y, 1, 0.5);

                        // 3. Pop both style rules off the stack to clean up the pipeline
                        ImPlot::PopStyleColor(2);
                    }
                }
                ImPlot::EndPlot();
            }

            // Setup Historical Line Chart (Bottom Half)
            if (ImPlot::BeginPlot("Historical Band Powers", ImVec2(-1, -1))) {
                ImPlot::SetupAxes("Window", "Log Amplitude", ImPlotAxisFlags_AutoFit, ImPlotScale_Log10);
                ImPlot::SetupAxesLimits(0, 50, 0.1, 100, ImPlotCond_Always);

                for (int i = 0; i < 5; i++) {
                    if (selectedWaves[i] && bandHistory[i].Data.size() > 0) {
                        ImPlot::PlotLine(waves[i], &bandHistory[i].Data[0], bandHistory[i].Data.size());
                    }
                }
                ImPlot::EndPlot();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::Columns(1);

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
    ImGui::End();
}

ImVec4 ColorConvertU32ToFloat4(ImU32 in) {
    float s = 1.0f / 255.0f;
    return ImVec4(
        ((in >> IM_COL32_R_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_G_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_B_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_A_SHIFT) & 0xFF) * s);
}

void UIManager::Cleanup() {
    if (window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        window = nullptr;
    }
}
