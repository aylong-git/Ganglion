#include "UI.h"
#include "implot.h"

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
    glfwSwapInterval(1); // Enable vsync

    // 3. Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 4. Load Fonts
    const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
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

    // If the window closes, tell the rest of the program to shut down
    isRunning = false;
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

    // Connection Inputs
    ImGui::InputText("MAC", macAddress, IM_ARRAYSIZE(macAddress));
    const char* ports[] = { "COM1", "COM2", "COM3", "COM4" }; // Update dynamically in reality
    ImGui::Combo("Port", &selectedPort, ports, IM_ARRAYSIZE(ports));

    ImGui::Spacing();

    // Connect Button
    if (isConnected) {
        if (ImGui::Button("Disconnect", ImVec2(-1, 40))) {
            reqDisconnect = true; // main.cpp will see this and disconnect!
        }
    }
    else {
        if (ImGui::Button("Connect", ImVec2(-1, 40))) {
            reqConnect = true; // main.cpp will see this and connect!
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // Control Settings (Moved from Python Option Frame)
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
            ImGui::Text("\nStatus: %s", (currentConcentration >= focusThreshold) ? "Focusing" : "Not Focusing");
            ImGui::Spacing(); ImGui::Spacing();

            // Concentration Line Plot
            if (ImPlot::BeginPlot("Concentration Level", ImVec2(-1, -1))) {
                ImPlot::SetupAxes("Window", "Concentration Level", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_Lock);
                ImPlot::SetupAxesLimits(0, 40, 0.0, 1.0, ImPlotCond_Always);

                // Plot history line
                if (concentrationHistory.Data.size() > 0) {
                    ImPlot::PlotLine("Level", &concentrationHistory.Data[0], concentrationHistory.Data.size());
                }

                // Plot red threshold horizontal line
                double threshLineX[2] = { concentrationHistory.Data.front(), concentrationHistory.Data.back() };
                double threshLineY[2] = { focusThreshold, focusThreshold };
                ImPlotSpec spec;
                spec.LineColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                spec.LineWeight = 2.0f;
                ImPlot::PlotLine("Threshold Target", threshLineX, threshLineY, 2, spec);

                ImPlot::EndPlot();
            }
            ImGui::EndTabItem();
        }

        // ------------------------------------------
        // TAB 2: IMPEDANCE CHECK
        // ------------------------------------------
        if (ImGui::BeginTabItem("Impedance Check")) {
            ImGui::Spacing();

            if (ImGui::Button(isCheckingImpedance ? "Stop Checking" : "Check Impedance", ImVec2(200, 40))) {
                isCheckingImpedance = !isCheckingImpedance;
                // Trigger impedance Python/BrainFlow routine here
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            const char* chanNames[5] = { "1", "2", "3", "4", "REF" };
            for (int i = 0; i < 5; i++) {
                // Determine color based on impedance (Python logic mapping)
                ImU32 col = IM_COL32(255, 0, 0, 255); // Default Red
                if (impedanceValues[i] <= 20)      col = IM_COL32(34, 90, 8, 255); // Dark Green
                else if (impedanceValues[i] <= 50) col = IM_COL32(0, 255, 0, 255); // Bright Green
                else if (impedanceValues[i] <= 100)col = IM_COL32(255, 255, 0, 255);// Yellow
                else if (impedanceValues[i] <= 200)col = IM_COL32(255, 165, 0, 255);// Orange

                // Draw circle indicator
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 10, p.y + 10), 8.0f, col);

                ImGui::SetCursorScreenPos(ImVec2(p.x + 30, p.y));
                ImGui::Text("Channel %s: %.1f kΩ", chanNames[i], impedanceValues[i]);
                ImGui::Spacing();
            }
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
    ImGui::End();
}

// Helper to convert hex colors to ImVec4 for ImPlot
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

