#include <iostream>
#include <thread>
#include <atomic>
#include "UI/UI.h"
#include "Processing/BrainflowAlgorithm.h"

int main() {
    // Initialize our two main systems
    UIManager uiManager;
    BrainflowAlgorithm backend;

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
            if (uiManager.reqConnect) {
                // Get the text from the UI Input Boxes
                std::string port = uiManager.availablePorts[uiManager.selectedPortIdx]; // or port array depending on your UI layout
                std::string mac = uiManager.macAddress;

                bool success = backend.Connect(port, mac);
                uiManager.isConnected = success;
                uiManager.reqConnect = false;
            }

            if (uiManager.reqDisconnect) {
                backend.Disconnect();
                uiManager.isConnected = false;
                uiManager.reqDisconnect = false;
            }

            // 2. Process Data if Connected
            if (backend.IsConnected()) {

                // Convert boolean UI checkboxes into a list of active channels (1, 2, 3, 4)
                std::vector<int> activeChannels;
                for (int i = 0; i < 4; i++) {
                    if (uiManager.eegChannels[i]) activeChannels.push_back(i + 1);
                }

                // Crunch the numbers!
                backend.ProcessData(activeChannels);

                // 3. Push new data back to the UI
                uiManager.currentConcentration = backend.GetConcentration();
                uiManager.concentrationHistory.AddPoint(uiManager.currentConcentration);

                std::vector<double> bands = backend.GetBandPowers();
                for (int i = 0; i < 5; i++) {
                    uiManager.currentBandPowers[i] = bands[i];
                    uiManager.bandHistory[i].AddPoint((float)bands[i]);
                }
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

    backend.Disconnect();
    uiManager.Cleanup();

    return 0;
}