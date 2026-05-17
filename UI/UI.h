#pragma once

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <map>
#include <atomic>
#include <iostream>
#include <string>
#include <vector>

// Add a simple scrolling buffer struct for ImPlot historical data
struct ScrollingBuffer {
    int MaxSize;
    int Offset;
    std::vector<float> Data;
    ScrollingBuffer(int max_size = 50) {
        MaxSize = max_size;
        Offset = 0;
        Data.reserve(MaxSize);
    }
    void AddPoint(float y) {
        if (Data.size() < MaxSize) Data.push_back(y);
        else { Data[Offset] = y; Offset = (Offset + 1) % MaxSize; }
    }
    void Erase() { if (Data.size() > 0) { Data.clear(); Offset = 0; } }
};

class UIManager {
public:
    UIManager();
    ~UIManager();
    bool Initialize();
    void Run(std::atomic<bool>& isRunning);
    void Cleanup();

    // --- Connection Triggers and State ---
    char macAddress[64] = "";
    int selectedPort = 0;
    bool isConnected = false;

    // Asynchronous flags to signal main.cpp when buttons are clicked
    bool reqConnect = false;
    bool reqDisconnect = false;

    // --- Control Settings (Read by main.cpp) ---
    bool eegChannels[4] = { true, true, false, false }; // Ch 1, 2, 3, 4
    int inputModeIdx = 0;                               // 0 = Press, 1 = Hold
    float focusThreshold = 0.75f;
    float statusRefreshTime = 0.1f;
    std::string input1Text = "Up";
    std::string input2Text = "Down";

    // --- Calculated Metrics (Written by main.cpp) ---
    float currentConcentration = 0.0f;
    ScrollingBuffer concentrationHistory{ 40 };

    // --- Band Power Arrays (Written by main.cpp) ---
    bool selectedWaves[5] = { true, true, true, true, true };
    float currentBandPowers[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    ScrollingBuffer bandHistory[5];

    // --- Impedance Tab State ---
    bool isCheckingImpedance = false;
    float impedanceValues[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

private:
    GLFWwindow* window;
    std::map<int, ImFont*> fonts;

    // Internal UI functions (main.cpp doesn't need to call these)
    void SetupCustomTheme();
    void RenderUI();
};