#include "GanglionHandler.h"
#include <iostream>
#include "data_filter.h"
#include "InputManager.h"
#include "Utils.h"
#include <algorithm>

GanglionHandler::GanglionHandler() : board(nullptr), connected(false) {
    currentConcentration = 0.0f;
    currentBandPowers = { 0.0, 0.0, 0.0, 0.0, 0.0 };
}

GanglionHandler::~GanglionHandler() {
    Disconnect();
}

bool GanglionHandler::Connect(const std::string& port, const std::string& macAddress) {
    if (connected) return true;

    try {
        BoardShim::enable_dev_board_logger();

        if (board == nullptr) {
            struct BrainFlowInputParams params;

            if (!port.empty()) params.serial_port = port;
            if (!macAddress.empty()) params.mac_address = macAddress;

            boardId = (int)BoardIds::GANGLION_BOARD;
            samplingRate = BoardShim::get_sampling_rate(boardId);

            board = new BoardShim(boardId, params);
        }

        if (!board->is_prepared()) {
            board->prepare_session();
        }

        board->start_stream(450000, "file://Raw_EEG_Session.csv:w");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        board->get_board_data();
        connected = true;
        std::cout << "[BrainFlow] Connected successfully to Ganglion.\n";
        return true;

    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow Error] " << err.what() << std::endl;
        Disconnect();
        return false;
    }
}

void GanglionHandler::Disconnect() {
    if (board != nullptr) {
        try {
            if (board->is_prepared()) {
                board->stop_stream();
                board->release_session();
            }
        }
        catch (...) {}
        delete board;
        board = nullptr;
    }

    connected = false;
    std::cout << "[BrainFlow] Disconnected.\n";
}

bool GanglionHandler::IsConnected() const {
    return connected.load();
}

void GanglionHandler::ProcessData(const std::vector<int>& activeChannels) {
    if (!connected || board == nullptr || activeChannels.empty()) return;

    try {
        int numSamples = board->get_board_data_count();
        if (numSamples < 300) return; // Wait until we have enough data

        // 1. Modern API: Returns a BrainFlowArray directly (no dataCount needed)
        BrainFlowArray<double, 2> data = board->get_current_board_data(numSamples);

        // 2. Modern API: Returns a std::vector<int> directly (no dataCount needed)
        std::vector<int> allEegChannels = BoardShim::get_eeg_channels(boardId);

        // Map UI channel selection (1, 2, 3, 4) to actual Ganglion EEG rows
        std::vector<int> eegRows;
        for (int ch : activeChannels) {
            eegRows.push_back(allEegChannels[ch - 1]); // Convert 1-based UI to 0-based index
        }

        // 3. Apply filters to selected channels
        for (int row : eegRows) {
            // BrainFlowArray provides .get_address(row) to extract the raw pointer for the filters
            DataFilter::detrend(data.get_address(row), numSamples, (int)DetrendOperations::LINEAR);
            DataFilter::perform_bandstop(data.get_address(row), numSamples, samplingRate, 48.0, 52.0, 4, (int)FilterTypes::BUTTERWORTH, 0);
            DataFilter::perform_bandstop(data.get_address(row), numSamples, samplingRate, 58.0, 62.0, 4, (int)FilterTypes::BUTTERWORTH, 0);
            DataFilter::perform_bandpass(data.get_address(row), numSamples, samplingRate, 5.0, 15.0, 4, (int)FilterTypes::BESSEL, 0);
        }

        // 4. Calculate Band Powers
        // BrainFlow returns a pair of raw double arrays (size 5 each: Delta, Theta, Alpha, Beta, Gamma)
        std::pair<double*, double*> bands = DataFilter::get_avg_band_powers(data, eegRows, samplingRate, true);

        // Extract Theta and Beta
        double theta = bands.first[1];
        double beta = bands.first[3];
        double rawTBRatio = (beta > 0) ? (theta / beta) : 0.0;

        double minRatio = 0.3;
        double maxRatio = 2.5;

        double TBRatio = (rawTBRatio - minRatio) / (maxRatio - minRatio);
        TBRatio = std::clamp(TBRatio, 0.0, 1.0);
        double concentrationIndex = 1.0 - TBRatio;

        auto& inputMgr = InputManager::GetInstance();
        float focusThreshold = Config::focusThreshold.load();
        int inputModeHold = Config::inputModeHold.load();

        InputManager::Target State = (concentrationIndex >= focusThreshold) ? InputManager::Target::Focus : InputManager::Target::Relax;

        switch (inputModeHold) {
        case 0:
            if (!inputMgr.IsBinding()) {
                OSInputSimulator::SimulateTap(State);
            }
            break;

        case 1:
            if (!inputMgr.IsBinding()) {
                OSInputSimulator::SimulateHold(State);
            }
            break;

        default:
            break;
        }

        // 6. Safely store the results for the UI to read
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            currentConcentration = (float)concentrationIndex;

            for (int i = 0; i < 5; i++) {
                currentBandPowers[i] = bands.first[i];
            }
        }

        // 7. CRITICAL CLEANUP
        // Because get_avg_band_powers returns raw arrays allocated on the heap, 
        // we must manually delete them to prevent severe memory leaks!
        delete[] bands.first;
        delete[] bands.second;
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow Processing Error] " << err.what() << std::endl;
    }
}

bool GanglionHandler::StartImpedanceMode(const std::string& port, const std::string& macAddress) {
    if (board == nullptr) {
        struct BrainFlowInputParams params;

        if (!port.empty()) params.serial_port = port;
        if (!macAddress.empty()) params.mac_address = macAddress;

        boardId = (int)BoardIds::GANGLION_BOARD;
        samplingRate = BoardShim::get_sampling_rate(boardId);

        board = new BoardShim(boardId, params);
    }

    try {
        board->prepare_session();
        board->start_stream();
        board->config_board("z");

        std::lock_guard<std::mutex> lock(dataMutex);
        impedanceMode = true;
        return true;
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow] Failed to start impedance check: " << err.what() << std::endl;
        return false;
    }
}

void GanglionHandler::StopImpedanceMode() {
    if (board == nullptr || !impedanceMode) return;

    try {
        board->config_board("Z");
        board->stop_stream();
        board->release_session();

        std::lock_guard<std::mutex> lock(dataMutex);
        impedanceMode = false;
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow] Failed to stop impedance check: " << err.what() << std::endl;
    }
}

bool GanglionHandler::IsImpedanceMode() const {
    return impedanceMode.load();
}

void GanglionHandler::UpdateImpedanceData() {
    if (board == nullptr) return;

    try {
        int data_count = board->get_board_data_count();
        if (data_count == 0) return;

        BrainFlowArray<double, 2> data = board->get_current_board_data(1);
        std::vector<int> resistance_rows = BoardShim::get_resistance_channels(boardId);

        {
            std::lock_guard<std::mutex> lock(dataMutex);

            for (size_t i = 0; i < resistance_rows.size() && i < currentImpedances.size(); ++i) {
                int row_index = resistance_rows[i];
                currentImpedances[i] = data.get_address(row_index)[0] / 2.0;
            }
        }
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow Impedance Fetch Error] " << err.what() << std::endl;
    }
}

std::vector<float> GanglionHandler::GetLatestImpedance() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentImpedances;
}

float GanglionHandler::GetConcentration() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentConcentration;
}

std::vector<double> GanglionHandler::GetBandPowers() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentBandPowers;
}
