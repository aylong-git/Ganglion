#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

#include "board_shim.h"
#include "data_filter.h"
#include "ml_model.h"

class BrainflowAlgorithm {
public:
    BrainflowAlgorithm();
    ~BrainflowAlgorithm();

    // Connection Controls
    bool Connect(const std::string& port, const std::string& mac, const std::vector<int>& selected_eeg_channels);
    void Disconnect();

    // Thread-safe variables for the ImGui Frontend to read
    std::atomic<bool> isConnected{ false };
    std::atomic<double> concentrationMeasure{ 0.0 };
    std::atomic<int> boardId{ (int)BoardIds::GANGLION_BOARD };
    int samplingRate = 200;

private:
    BoardShim* board;
    std::vector<int> activeEegChannels;

    // The background loop (Equivalent to your Python ConnectedTask)
    void DataLoop();
};