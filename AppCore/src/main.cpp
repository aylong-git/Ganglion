#include <iostream>
#include <algorithm>
#include <thread>
#include <atomic>
#include "UI.h"
#include "GanglionHandler.h"
#include "EEGRecorder.h"
#include "HeadObject.h"
#include <chrono>

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" {
    // Forces NVIDIA Optimus laptops to use the high-performance GPU
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    // Forces AMD PowerXpress laptops to use the high-performance GPU
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main() {
    // Initialize our two main systems
    UIManager uiManager;
    GanglionHandler ganglionBoard;
    EEGRecorder eegRecorder;
    HeadObject head_object;

    if (!uiManager.Initialize()) {
        std::cerr << "Failed to initialize UI. Exiting.\n";
        return -1;
    }
    ganglionBoard.SetRecorder(&eegRecorder);

    std::atomic<bool> isRunning(true);

    // ==========================================
    // THE BACKGROUND WORKER THREAD
    // ==========================================
    std::thread workerThread([&]() {
        auto lastSnapshotTime = std::chrono::high_resolution_clock::now();

        while (isRunning) {
            auto loopStart = std::chrono::high_resolution_clock::now();

            // 1. Handle UI Button Presses
            if (uiManager.reqConnect || uiManager.reqImpedanceCheck) {
                std::string port;
                std::string mac = uiManager.macAddress;
                const int portIdx = uiManager.selectedPortIdx;
                if (portIdx >= 0 && portIdx < static_cast<int>(uiManager.availablePorts.size())) {
                    port = uiManager.availablePorts[portIdx];
                }

                if (mac.find(":::::") != std::string::npos) mac = "";

                bool success = false;
                if (port.empty() || port == "No Dongle Found") {
                    uiManager.portErrorMessage = "No dongle port selected. Plug in the USB dongle and click Refresh.";
                }
                else {
                    success = (uiManager.reqConnect) ? ganglionBoard.Connect(port, mac) : ganglionBoard.StartImpedanceMode(port, mac);
                    if (!success) {
                        const std::string detail = ganglionBoard.LastConnectError();
                        uiManager.portErrorMessage = "Failed to open " + port +
                            ".\n\nBrainFlow: " + (detail.empty() ? "unknown error (check the console)" : detail);
                    }
                }

                if (success) {
                    if (uiManager.reqConnect) {
                        uiManager.isConnected = true;
                    }
                    else {
                        uiManager.isCheckingImpedance = true;
                    }
                }
                else {
                    uiManager.isConnected = false;
                    uiManager.isCheckingImpedance = false;
                    uiManager.showPortErrorPopup = true;
                    ganglionBoard.Disconnect();
                }
                uiManager.reqConnect = false;
                uiManager.reqImpedanceCheck = false;
            }

            if (uiManager.reqDisconnect) {
                eegRecorder.Stop();
                uiManager.isRecording.store(false);
                ganglionBoard.Disconnect();
                uiManager.isConnected = false;
                uiManager.reqDisconnect = false;
            }

            if (uiManager.reqStartRecord) {
                const std::string path = uiManager.GetFullRecordPath();
                if (eegRecorder.Start(path)) {
                    uiManager.isRecording.store(true);
                }
                else {
                    uiManager.recordErrorMessage = "Failed to open:\n" + path;
                    uiManager.showRecordErrorPopup = true;
                    uiManager.isRecording.store(false);
                }
                uiManager.reqStartRecord = false;
            }

            if (uiManager.reqStopRecord) {
                eegRecorder.Stop();
                uiManager.isRecording.store(false);
                uiManager.reqStopRecord = false;
            }

            // 2. Process Data if Connected
            if (ganglionBoard.IsConnected()) {

                // Convert boolean UI checkboxes into a list of active channels (1, 2, 3, 4)
                std::vector<int> activeChannels;
                for (int i = 0; i < 4; i++) {
                    if (uiManager.eegChannels[i]) activeChannels.push_back(i + 1);
                }
                ganglionBoard.SetMonitorRefreshHz(uiManager.monitorRefreshHz.load());
                
                // 1. Sync the Window Duration from the UI Slider
                ganglionBoard.SetSSVEPWindow(uiManager.SSVEPWindowDuration);

                // 2. Sync Target Frequencies (Accounting for Monitor Snapping & Contrast Mode)
                float brainHz[4];
                int refresh = uiManager.monitorRefreshHz.load();
                if (refresh < 30) refresh = 60; // Safety fallback

                for (int i = 0; i < 4; ++i) {
                    bool contrast = SSVEPPatternHasContrast(uiManager.SSVEPBoardLayout.patternMode)
                        && uiManager.SSVEPBoardLayout.contrastMode;
                    // The physical screen flicker target
                    float screenTarget = contrast ? (uiManager.SSVEPBoardLayout.customHz[i] / 2.0f) : uiManager.SSVEPBoardLayout.customHz[i];
                    
                    // Exact physical monitor frames per cycle
                    int periodFrames = std::max(2, static_cast<int>(std::lround(static_cast<double>(refresh) / screenTarget)));
                    float realizedScreenHz = static_cast<float>(refresh) / static_cast<float>(periodFrames);
                    
                    // The SSVEP brain response is double the visual flicker in contrast mode
                    brainHz[i] = contrast ? (realizedScreenHz * 2.0f) : realizedScreenHz;
                }
                ganglionBoard.SetTargetFrequencies(brainHz);
                std::chrono::duration<double, std::milli> elapsedSnapshot = loopStart - lastSnapshotTime;
                bool updatePlot = false;
                if (uiManager.signalTabActive.load() && elapsedSnapshot.count() >= 33.3) {
                    lastSnapshotTime = loopStart;
                    updatePlot = true;
                }

                ganglionBoard.ProcessData(activeChannels, updatePlot);

                std::vector<double> bands = ganglionBoard.GetBandPowers();
                for (int i = 0; i < 5; i++) {
                    uiManager.currentBandPowers[i] = bands[i];
                }

                if (updatePlot) {
                    EEGPlotSnapshot snap = ganglionBoard.GetEEGPlotSnapshot();
                    SSVEPDetectSnapshot detect = ganglionBoard.GetSSVEPSnapshot();
                    std::lock_guard<std::mutex> lock(uiManager.eegPlotMutex);
                    uiManager.eegPlot = std::move(snap);
                    uiManager.SSVEPDetect = std::move(detect);
                }
            }
            else if (uiManager.isRecording.load()) {
                eegRecorder.Stop();
                uiManager.isRecording.store(false);
            }

            if (uiManager.reqStopImpedanceCheck) {
                ganglionBoard.StopImpedanceMode();
                uiManager.isCheckingImpedance = false;
                uiManager.reqStopImpedanceCheck = false;
            }

            if (ganglionBoard.IsImpedanceMode()) {
                ganglionBoard.UpdateImpedanceData();

                std::vector<float> latestImps = ganglionBoard.GetLatestImpedance();
                std::copy(latestImps.begin(), latestImps.end(), uiManager.currentImpedances.begin());
            }

            // Sleep to prevent maxing out the CPU loop
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        });

    // ==========================================
    // START THE UI LOOP (Blocks until window closes)
    // ==========================================
    uiManager.Run(isRunning);

    // ==========================================
    // SHUTDOWN ROUTINE
    // ==========================================
    isRunning = false;
    if (workerThread.joinable()) {
        workerThread.join();
    }

    eegRecorder.Stop();
    ganglionBoard.Disconnect();
    uiManager.Cleanup();

    return 0;
}
