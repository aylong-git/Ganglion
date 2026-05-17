#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#include "board_shim.h"
#include "data_filter.h"
#include "ml_model.h"

class BrainflowAlgorithm {
public:
    BrainflowAlgorithm();
    ~BrainflowAlgorithm();

    // Core Board Commands
    bool Connect(const std::string& port, const std::string& macAddress);
    void Disconnect();
    bool IsConnected() const;

    // The main processing loop (Gets data, filters, runs ML)
    void ProcessData(const std::vector<int>& activeChannels);

    // Thread-safe Getters for the UI
    float GetConcentration();
    std::vector<double> GetBandPowers();

private:
    BoardShim* board;
    MLModel* concentrationModel;
    std::atomic<bool> connected;

    int samplingRate;
    int boardId;

    // Thread-safety locks and shared data
    std::mutex dataMutex;
    float currentConcentration;
    std::vector<double> currentBandPowers; // Delta, Theta, Alpha, Beta, Gamma
};