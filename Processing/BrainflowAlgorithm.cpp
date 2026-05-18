#include "BrainflowAlgorithm.h"
#include <iostream>
#include <thread>

BrainflowAlgorithm::BrainflowAlgorithm() : board(nullptr), concentrationModel(nullptr), connected(false) {
    currentConcentration = 0.0f;
    currentBandPowers = { 0.0, 0.0, 0.0, 0.0, 0.0 };
}

BrainflowAlgorithm::~BrainflowAlgorithm() {
    Disconnect();
}

bool BrainflowAlgorithm::Connect(const std::string& port, const std::string& macAddress) {
    if (connected) return true;

    try {
        BoardShim::enable_dev_board_logger();
        struct BrainFlowInputParams params;

        // Convert std::string to char array for BrainFlow
        if (!port.empty()) params.serial_port = port;
        if (!macAddress.empty()) params.mac_address = macAddress;

        boardId = (int)BoardIds::GANGLION_BOARD;
        samplingRate = BoardShim::get_sampling_rate(boardId);

        board = new BoardShim(boardId, params);
        board->prepare_session();
        board->start_stream();

        // Initialize ML Model
        struct BrainFlowModelParams modelParams((int)BrainFlowMetrics::MINDFULNESS, (int)BrainFlowClassifiers::DEFAULT_CLASSIFIER);
        concentrationModel = new MLModel(modelParams);
        concentrationModel->prepare();

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

void BrainflowAlgorithm::Disconnect() {
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

    if (concentrationModel != nullptr) {
        try { concentrationModel->release(); }
        catch (...) {}
        delete concentrationModel;
        concentrationModel = nullptr;
    }

    connected = false;
    std::cout << "[BrainFlow] Disconnected.\n";
}

bool BrainflowAlgorithm::IsConnected() const {
    return connected.load();
}

void BrainflowAlgorithm::ProcessData(const std::vector<int>& activeChannels) {
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
            DataFilter::perform_bandstop(data.get_address(row), numSamples, samplingRate, 48.0, 52.0, 4, (int)FilterTypes::BUTTERWORTH, 0);
            DataFilter::perform_bandstop(data.get_address(row), numSamples, samplingRate, 58.0, 62.0, 4, (int)FilterTypes::BUTTERWORTH, 0);
            DataFilter::perform_bandpass(data.get_address(row), numSamples, samplingRate, 5.0, 15.0, 4, (int)FilterTypes::BESSEL, 0);
        }

        // 4. Calculate Band Powers
        // BrainFlow returns a pair of raw double arrays (size 5 each: Delta, Theta, Alpha, Beta, Gamma)
        std::pair<double*, double*> bands = DataFilter::get_avg_band_powers(data, eegRows, samplingRate, true);

        // Combine them into a continuous feature vector for the ML model (5 averages + 5 stddevs = 10 elements)
        std::vector<double> feature_vector(10);
        for (int i = 0; i < 5; i++) {
            feature_vector[i] = bands.first[i];       // 0-4: Averages
            feature_vector[i + 5] = bands.second[i];  // 5-9: Standard Deviations
        }

        // 5. Run the ML Prediction
        // Capture the returned std::vector<double> from the model
        std::vector<double> prediction = concentrationModel->predict(feature_vector.data(), (int)feature_vector.size());

        // Safely extract the first element if the vector isn't empty
        double concentration = prediction.empty() ? 0.0 : prediction[0];

        // 6. Safely store the results for the UI to read
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            currentConcentration = (float)concentration;

            for (int i = 0; i < 5; i++) {
                currentBandPowers[i] = feature_vector[i];
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

float BrainflowAlgorithm::GetConcentration() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentConcentration;
}

std::vector<double> BrainflowAlgorithm::GetBandPowers() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentBandPowers;
}