#pragma once
#include <map>
#include <atomic>
#include <string>
#include <vector>
#include <cmath>
#include <array>
#include <GLFW/glfw3.h>
#include "HeadObject.h"

struct ScrollingBuffer {
    int MaxSize;
    int Offset;
    unsigned long long TotalPoints;
    std::vector<float> Data;

    ScrollingBuffer(int max_size = 50) {
        MaxSize = max_size;
        Offset = 0;
        TotalPoints = 0;
        Data.reserve(MaxSize);
    }

    void AddPoint(float y) {
        TotalPoints++;
        if (Data.size() < MaxSize) {
            Data.push_back(y);
        }
        else {
            Data[Offset] = y;
            Offset = (Offset + 1) % MaxSize;
        }
    }

    void Erase() { if (Data.size() > 0) { Data.clear(); Offset = 0; TotalPoints = 0; } }
};

class UIManager {
public:
    UIManager();
    ~UIManager();

    HeadObject headObject;
    bool Initialize();
    void Run(std::atomic<bool>& isRunning);
    void Cleanup();
    ImVec4 ColorConvertU32ToFloat4(ImU32 in);

    // Connection Panel
    char macParts[6][3] = { "", "", "", "", "", "" };
    char macAddress[18] = "";
    unsigned int macHelpTextureID = 0;
    int selectedPortIdx = 0;
    std::vector<std::string> availablePorts;
    bool isConnected = false;
    void UpdateAvailablePorts();

    bool showPortErrorPopup = false;
    std::string portErrorMessage = "";

    // Asynchronous flags
    bool reqConnect = false;
    bool reqDisconnect = false;

    // Control Settings
    bool eegChannels[4] = { true, true, false, false };
    float statusRefreshTime = 0.1f;

    float currentConcentration = 0.0f;
    double xRange = 2.0;
    ScrollingBuffer concentrationHistory{ static_cast<int>(std::round(200.0f * xRange)) };

    // Band Power Arrays
    bool selectedWaves[5] = { true, true, true, true, true };
    float currentBandPowers[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    // Impedance Tab
    bool isCheckingImpedance = false;
    std::array<float, 5> currentImpedances = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool reqImpedanceCheck = false;
    bool reqStopImpedanceCheck = false;

private:
    GLFWwindow* window;
    std::map<int, ImFont*> fonts;

    void SetupCustomTheme();
    void RenderUI();
};