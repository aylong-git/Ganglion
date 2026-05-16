#include "BrainflowAlgorithm.h"
#include <iostream>

BrainflowAlgorithm::BrainflowAlgorithm() {
    board = nullptr;
}

BrainflowAlgorithm::~BrainflowAlgorithm() {
    if (isConnected) {
        Disconnect();
    }
}

bool BrainflowAlgorithm::Connect(const std::string& port, const std::string& mac, const std::vector<int>& selected_eeg_channels) {
    if (isConnected) return true;

    activeEegChannels = selected_eeg_channels;

    BrainFlowInputParams params;
    params.serial_port = port;
    if (!mac.empty()) {
        params.mac_address = mac;
    }

    board = new BoardShim(boardId, params);

    try {
        board->prepare_session();
        board->start_stream();
        isConnected = true;

        // Start the background thread (Matches threading.Thread in Python)
        std::thread(&BrainflowAlgorithm::DataLoop, this).detach();
        return true;
    }
    catch (const BrainFlowException& err) {
        std::cerr << "BrainFlow Error: " << err.what() << std::endl;
        delete board;
        board = nullptr;
        return false;
    }
}

void BrainflowAlgorithm::Disconnect() {
    isConnected = false; // This safely stops the DataLoop thread
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give thread time to exit

    if (board != nullptr) {
        try {
            board->stop_stream();
            board->release_session();
        }
        catch (...) {} // Ignore cleanup errors

        delete board;
        board = nullptr;
    }
}

void BrainflowAlgorithm::DataLoop() {
    // ML Model Setup (Matches BrainFlowMetrics.MINDFULNESS)
    BrainFlowModelParams ml_params((int)BrainFlowMetrics::MINDFULNESS, (int)BrainFlowClassifiers::DEFAULT_CLASSIFIER);
    MLModel concentration_model(ml_params);
    concentration_model.prepare();

    while (isConnected) {
        // 1. Wait until we have 300 samples
        if (board->get_board_data_count() < 300) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // 2. Fetch the data using BrainFlow v5's smart array
        BrainFlowArray<double, 2> data = board->get_current_board_data(300);
        int num_samples = data.get_size(1); // Get the number of columns (samples)

        // 3. Apply Filters for each active channel
        for (int channel : activeEegChannels) {
            // We use .get_address(channel) to give the filter the raw memory pointer it needs
            double* channel_ptr = data.get_address(channel);

            // Notch Filter ~50Hz
            DataFilter::perform_bandstop(channel_ptr, num_samples, samplingRate, 48.0, 52.0, 4, (int)FilterTypes::BUTTERWORTH, 0);
            // Notch Filter ~60Hz
            DataFilter::perform_bandstop(channel_ptr, num_samples, samplingRate, 58.0, 62.0, 4, (int)FilterTypes::BUTTERWORTH, 0);
            // Bandpass Filter (5.0 - 15.0)
            DataFilter::perform_bandpass(channel_ptr, num_samples, samplingRate, 5.0, 15.0, 4, (int)FilterTypes::BESSEL, 0);
        }

        // 4. Calculate Band Powers
        // The v5 API beautifully accepts the BrainFlowArray and std::vector directly!
        // It returns a std::pair of vectors (first = averages, second = std_devs)
        auto bands = DataFilter::get_avg_band_powers(data, activeEegChannels, samplingRate, true);

        // 5. Concatenate into a 1D Feature Vector (5 avg + 5 stddev = 10 features)
        std::vector<double> feature_vector(10);
        for (int i = 0; i < 5; i++) {
            feature_vector[i] = bands.first[i];     // Average band powers
            feature_vector[i + 5] = bands.second[i]; // Standard deviation
        }

        // NOTE: No manual delete[] memory cleanup is needed here anymore! 
        // BrainFlowArray and std::pair clean themselves up automatically.

        // Use .data() to give the function the raw double* it wants, and .size() for the length
        double* prediction = &concentration_model.predict(feature_vector.data(), (int)feature_vector.size())[0];

        // Make sure the prediction actually worked and returned data
        if (prediction != nullptr) {
            concentrationMeasure = *prediction; // Update the atomic variable

            // Because MLModel still uses the old API, we MUST manually delete this!
            delete[] prediction;
        }
    }

    concentration_model.release();
}