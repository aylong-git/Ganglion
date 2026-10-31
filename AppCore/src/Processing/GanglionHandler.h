#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>

#include "board_shim.h"
#include "SSVEPClassifier.h"
#include "SSVEPTargets.h"

class EEGRecorder;

static constexpr int kEEGPlotDisplayCapacity = 1000; // ~5 s at 200 Hz shown in Signal
// Order-6 Butterworth startup lasts about (order / low_cutoff) seconds.
// The plot edge can fall to 0.5 Hz, which needs ~12 s. The visible window
// still shows the latest ~5 s after that prefix is dropped.
static constexpr int kEEGFilterOrder = 6;
static constexpr int kEEGFilterWarmupCapacity = 12 * 200;
static constexpr int kEEGPlotCapacity = kEEGPlotDisplayCapacity + kEEGFilterWarmupCapacity;

struct EEGPlotSnapshot {
    int samplingRate = 200;
    std::array<int, 4> count{};
    std::array<bool, 4> hadData{};
    std::array<std::array<float, kEEGPlotCapacity>, 4> t{};
    std::array<std::array<float, kEEGPlotCapacity>, 4> y{};
};

class GanglionHandler {
public:
    GanglionHandler();
    ~GanglionHandler();

    // Core Board Commands
    bool Connect(const std::string& port, const std::string& macAddress);
    void Disconnect();
    bool IsConnected() const;

    void ProcessData(const std::vector<int>& activeChannels, bool updatePlot);
    void SetRecorder(EEGRecorder* recorder);
    std::vector<double> GetBandPowers();
    EEGPlotSnapshot GetEEGPlotSnapshot();
    void SetMonitorRefreshHz(int hz);
    void SetTargetFrequencies(const float hz[4]);
    void SetSSVEPWindow(float windowSeconds);
    SSVEPDetectSnapshot GetSSVEPSnapshot();

    bool StartImpedanceMode(const std::string& port, const std::string& macAddress);
    void StopImpedanceMode();
    bool IsImpedanceMode() const;

    void UpdateImpedanceData();
    std::vector<float> GetLatestImpedance();

    // BrainFlow exception text from the most recent Connect/impedance attempt.
    std::string LastConnectError() const;

private:
    struct EEGRing {
        std::array<float, kEEGPlotCapacity> t{};
        std::array<float, kEEGPlotCapacity> y{};
        int head = 0;
        int size = 0;
        bool hadData = false;
    };

    void ApplySSVEPPlotFilters(float* y, int n, int samplingRate, double lowHz, double highHz);
    void ClearEEGRingsLocked();
    void PushEEGSample(int channel, float timeSec, float valueUv);
    void TrySSVEPDetect(const bool enabled[4]);

    BoardShim* board = nullptr;
    std::atomic<bool> connected = false;

    std::atomic<bool> impedanceMode = false;

    int samplingRate = 200;
    int boardId = 0;

    // Thread-safety locks and shared data
    std::mutex dataMutex;
    std::vector<float> currentImpedances = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<double> currentBandPowers = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // Delta, Theta, Alpha, Beta, Gamma
    float currentTargetHz[4] = { kSSVEPTargetHz[0], kSSVEPTargetHz[1], kSSVEPTargetHz[2], kSSVEPTargetHz[3] };
    double plotBandLowHz = SSVEPPlotBandLowHz(kSSVEPTargetHz);
    double plotBandHighHz = SSVEPPlotBandHighHz(kSSVEPTargetHz);
    float currentWindowSec = static_cast<float>(kSSVEPWindowSec);
    std::string lastConnectError;

    std::array<EEGRing, 4> eegRings{};
    double sessionStartTs = -1.0;
    std::uint64_t sampleIndex = 0;
    std::uint64_t lastDetectSample = 0;
    bool lastDetectEnabled[4] = { false, false, false, false };
    int monitorRefreshHz = 60;
    SSVEPDetector ssvepDetector;
    SSVEPDetectSnapshot ssvepSnapshot;
    EEGRecorder* recorder = nullptr;
};
