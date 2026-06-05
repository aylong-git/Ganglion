#include <iostream>
#include <algorithm>
#include <thread>
#include <atomic>
#include "UI.h"
#include "GanglionHandler.h"
#include "HeadObject.h"

int main() {
    // Initialize our two main systems
    UIManager uiManager;
    GanglionHandler ganglionBoard;
    HeadObject head_object;

    if (!uiManager.Initialize()) {
        std::cerr << "Failed to initialize UI. Exiting.\n";
        return -1;
    }

    std::atomic<bool> isRunning(true);

    // ==========================================
    // THE BACKGROUND WORKER THREAD
    // ==========================================
    std::thread workerThread([&]() {
        while (isRunning) {

            // 1. Handle UI Button Presses
            if (uiManager.reqConnect || uiManager.reqImpedanceCheck) {
                // Get the text from the UI Input Boxes
                std::string port = uiManager.availablePorts[uiManager.selectedPortIdx];
                std::string mac = uiManager.macAddress;

                if (mac.find(":::::") != std::string::npos) mac = "";

                bool success = (uiManager.reqConnect) ? ganglionBoard.Connect(port, mac) : ganglionBoard.StartImpedanceMode(port, mac);
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
                    uiManager.portErrorMessage = "Port " + port + " is busy, invalid, or used by another device, try replugging.";
                    ganglionBoard.Disconnect();
                }
                uiManager.reqConnect = false;
                uiManager.reqImpedanceCheck = false;
            }

            if (uiManager.reqDisconnect) {
                ganglionBoard.Disconnect();
                uiManager.isConnected = false;
                uiManager.reqDisconnect = false;
            }

            if (uiManager.reqStopImpedanceCheck) {
                ganglionBoard.StopImpedanceMode();
                uiManager.isCheckingImpedance = false;
                uiManager.reqStopImpedanceCheck = false;
            }

            // 2. Process Data if Connected
            if (ganglionBoard.IsConnected()) {

                // Convert boolean UI checkboxes into a list of active channels (1, 2, 3, 4)
                std::vector<int> activeChannels;
                for (int i = 0; i < 4; i++) {
                    if (uiManager.eegChannels[i]) activeChannels.push_back(i + 1);
                }

                // Crunch the numbers!
                ganglionBoard.ProcessData(activeChannels);

                // 3. Push new data back to the UI
                uiManager.currentConcentration = ganglionBoard.GetConcentration();
                uiManager.concentrationHistory.AddPoint(uiManager.currentConcentration);

                std::vector<double> bands = ganglionBoard.GetBandPowers();
                for (int i = 0; i < 5; i++) {
                    uiManager.currentBandPowers[i] = bands[i];
                    uiManager.bandHistory[i].AddPoint((float)bands[i]);
                }
            }

            if (ganglionBoard.IsImpedanceMode()) {
                ganglionBoard.UpdateImpedanceData();

                std::vector<float> latestImps = ganglionBoard.GetLatestImpedance();
                std::copy(latestImps.begin(), latestImps.end(), uiManager.CurrentImpedances.begin());
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

    ganglionBoard.Disconnect();
    uiManager.Cleanup();

    return 0;
}