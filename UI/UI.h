#pragma once

#include "BrainflowAlgorithm.h"
#include <vector>
#include <string>

class UI {
public:
    // Pass the backend by reference so the UI can read the data
    UI(BrainflowAlgorithm& backendReference);

    // This function will be called every single frame in your main loop
    void Render();

private:
    BrainflowAlgorithm& backend;

    // --- UI State Variables (Replacing Tkinter variables) ---
    char portBuffer[16] = "COM3";
    char macBuffer[32] = "";
    bool channel1 = true;
    bool channel2 = true;
    bool channel3 = false;
    bool channel4 = false;
    float threshold = 0.75f;

    // --- Plotting Variables (Replacing Matplotlib lines) ---
    int maxPlotPoints = 200;
    std::vector<float> timeData;
    std::vector<float> concentrationData;
    float timeElapsed = 0.0f;
    float lastPlotUpdateTime = 0.0f;
};