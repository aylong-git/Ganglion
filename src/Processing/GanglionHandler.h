#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#include "board_shim.h"
#include "ml_model.h"

class GanglionHandler {
public:
    GanglionHandler();
    ~GanglionHandler();

    // Core Board Commands
    bool Connect(const std::string& port, const std::string& macAddress);
    void Disconnect();
    bool IsConnected() const;

    void ProcessData(const std::vector<int>& activeChannels);
    float GetConcentration();
    std::vector<double> GetBandPowers();

    bool StartImpedanceMode(const std::string& port, const std::string& macAddress);
    void StopImpedanceMode();
    bool IsImpedanceMode() const;

    void UpdateImpedanceData();
    std::vector<float> GetLatestImpedance();

private:
    BoardShim* board = nullptr;
    MLModel* concentrationModel = nullptr;
    std::atomic<bool> connected = false;

    std::atomic<bool> impedanceMode = false;

    int samplingRate;
    int boardId;

    // Thread-safety locks and shared data
    std::mutex dataMutex;
    float currentConcentration;
    std::vector<float> CurrentImpedances = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    std::vector<double> currentBandPowers; // Delta, Theta, Alpha, Beta, Gamma
};