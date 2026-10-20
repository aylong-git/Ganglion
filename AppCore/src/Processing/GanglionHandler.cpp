#include "GanglionHandler.h"
#include "EEGRecorder.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include "data_filter.h"

namespace {
    constexpr int kPlotFilterOrder = 6;
    constexpr int kPlotFilterMinSamples = 24;
}

void GanglionHandler::SetSSVEPWindow(float windowSeconds) {
    std::lock_guard<std::mutex> lock(dataMutex);
    currentWindowSec = windowSeconds;
    // For your immediate goal, hop ratio equals window ratio (no stride/overlap)
    ssvepDetector.SetWindowConfig(windowSeconds, windowSeconds); 
}

void GanglionHandler::SetTargetFrequencies(const float hz[4]) {
    std::lock_guard<std::mutex> lock(dataMutex);
    
    float minHz = hz[0];
    float maxHz = hz[0];
    for (int i = 0; i < 4; ++i) {
        currentTargetHz[i] = hz[i];
        if (hz[i] < minHz) minHz = hz[i];
        if (hz[i] > maxHz) maxHz = hz[i];
    }
    
    // Set dynamic bandpass ranges: min - 2.0 to max + 2.0
    plotBandLowHz = std::max(0.5, static_cast<double>(minHz - 2.0));
    plotBandHighHz = static_cast<double>(maxHz + 2.0);
    
    // Forward the frequencies to the CCA detector
    ssvepDetector.SetTargetFrequencies(hz);
}

void GanglionHandler::ApplySSVEPPlotFilters(float* y, int n, int samplingRate) {
    if (y == nullptr || n < kPlotFilterMinSamples || samplingRate <= 0) return;

    std::vector<double> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        buf[static_cast<size_t>(i)] = static_cast<double>(y[i]);
    }

    try {
        // Use the dynamically calculated bounds
        DataFilter::perform_bandpass(
            buf.data(), n, samplingRate, plotBandLowHz, plotBandHighHz,
            kPlotFilterOrder, (int)FilterTypes::BUTTERWORTH, 0.0);
        DataFilter::perform_bandstop(
            buf.data(), n, samplingRate, 48.0, 52.0,
            kPlotFilterOrder, (int)FilterTypes::BUTTERWORTH, 0.0);
        DataFilter::perform_bandstop(
            buf.data(), n, samplingRate, 58.0, 62.0,
            kPlotFilterOrder, (int)FilterTypes::BUTTERWORTH, 0.0);
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow Filter Error] " << err.what() << std::endl;
        return;
    }

    for (int i = 0; i < n; ++i) {
        y[i] = static_cast<float>(buf[static_cast<size_t>(i)]);
    }
}

GanglionHandler::GanglionHandler() : board(nullptr), connected(false) {
    currentBandPowers = { 0.0, 0.0, 0.0, 0.0, 0.0 };
}

void GanglionHandler::ClearEEGRingsLocked() {
    eegRings = {};
    sessionStartTs = -1.0;
    sampleIndex = 0;
    lastDetectSample = 0;
    ssvepDetector.Reset();
    ssvepSnapshot = {};
}

void GanglionHandler::PushEEGSample(int channel, float timeSec, float valueUv) {
    if (channel < 0 || channel >= 4) return;
    EEGRing& ring = eegRings[channel];
    ring.t[ring.head] = timeSec;
    ring.y[ring.head] = valueUv;
    ring.head = (ring.head + 1) % kEEGPlotCapacity;
    if (ring.size < kEEGPlotCapacity) ring.size++;
    ring.hadData = true;
}

GanglionHandler::~GanglionHandler() {
    Disconnect();
}

bool GanglionHandler::Connect(const std::string& port, const std::string& macAddress) {
    if (connected) return true;
    lastConnectError.clear();

    try {
        BoardShim::enable_dev_board_logger();
        std::cout << "[BrainFlow] Connecting port='" << port << "' mac='" << macAddress << "'\n";

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
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            ClearEEGRingsLocked();
        }
        connected = true;
        std::cout << "[BrainFlow] Connected successfully to Ganglion.\n";
        return true;

    }
    catch (const BrainFlowException& err) {
        lastConnectError = err.what() ? err.what() : "Unknown BrainFlow error";
        std::cerr << "[BrainFlow Error] " << lastConnectError << std::endl;
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

    {
        std::lock_guard<std::mutex> lock(dataMutex);
        ClearEEGRingsLocked();
    }
    connected = false;
    std::cout << "[BrainFlow] Disconnected.\n";
}

bool GanglionHandler::IsConnected() const {
    return connected.load();
}

void GanglionHandler::SetRecorder(EEGRecorder* rec) {
    recorder = rec;
}

void GanglionHandler::ProcessData(const std::vector<int>& activeChannels, bool updatePlot) {
    if (!connected || board == nullptr) return;

    try {
        int pending = board->get_board_data_count();
        if (pending <= 0) return;

        BrainFlowArray<double, 2> data = board->get_board_data();
        const int numSamples = data.get_size(1);
        if (numSamples <= 0) return;

        std::vector<int> allEEGChannels = BoardShim::get_eeg_channels(boardId);
        int tsRow = -1;
        try {
            tsRow = BoardShim::get_timestamp_channel(boardId);
        }
        catch (...) {
            tsRow = -1;
        }

        bool enabled[4] = { false, false, false, false };
        for (int ch : activeChannels) {
            if (ch >= 1 && ch <= 4) enabled[ch - 1] = true;
        }

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            if (samplingRate <= 0) samplingRate = 200;

            for (int s = 0; s < numSamples; ++s) {
                double tAbs = 0.0;
                if (tsRow >= 0) {
                    tAbs = data.get_address(tsRow)[s];
                }
                else {
                    tAbs = static_cast<double>(sampleIndex) / static_cast<double>(samplingRate);
                }
                if (sessionStartTs < 0.0) sessionStartTs = tAbs;
                const float tRel = static_cast<float>(tAbs - sessionStartTs);

                for (int ch = 0; ch < 4; ++ch) {
                    if (enabled[ch] && ch < static_cast<int>(allEEGChannels.size())) {
                        const float value = static_cast<float>(data.get_address(allEEGChannels[ch])[s]);
                        PushEEGSample(ch, tRel, value);
                    }
                }

                if (recorder && recorder->IsRecording()) {
                    float row[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    for (int ch = 0; ch < 4; ++ch) {
                        if (enabled[ch] && ch < static_cast<int>(allEEGChannels.size())) {
                            row[ch] = static_cast<float>(data.get_address(allEEGChannels[ch])[s]);
                        }
                    }
                    recorder->PushSample(row[0], row[1], row[2], row[3]);
                }
                ++sampleIndex;
            }
        }

        if (updatePlot) {
            std::vector<int> eegRows;
            eegRows.reserve(activeChannels.size());
            for (int ch : activeChannels) {
                if (ch >= 1 && ch <= static_cast<int>(allEEGChannels.size())) {
                    eegRows.push_back(allEEGChannels[ch - 1]);
                }
            }

            constexpr int kBandWin = 256;
            bool ringsReady = !eegRows.empty();
            {
                std::lock_guard<std::mutex> lock(dataMutex);
                for (int ch : activeChannels) {
                    if (ch < 1 || ch > 4 || eegRings[ch - 1].size < kBandWin) {
                        ringsReady = false;
                        break;
                    }
                }
            }

            if (ringsReady) {
                BrainFlowArray<double, 2> packed(static_cast<int>(eegRows.size()), kBandWin);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    int packedRow = 0;
                    for (int ch : activeChannels) {
                        if (ch < 1 || ch > 4) continue;
                        const EEGRing& ring = eegRings[ch - 1];
                        const int start = (ring.size == kEEGPlotCapacity) ? ring.head : 0;
                        const int first = ring.size - kBandWin;
                        double* dest = packed.get_address(packedRow++);
                        for (int i = 0; i < kBandWin; ++i) {
                            const int idx = (start + first + i) % kEEGPlotCapacity;
                            dest[i] = ring.y[idx];
                        }
                    }
                }

                std::vector<int> packedIdx(eegRows.size());
                for (size_t i = 0; i < packedIdx.size(); ++i) packedIdx[i] = static_cast<int>(i);

                std::pair<double*, double*> bands = DataFilter::get_avg_band_powers(
                    packed, packedIdx, samplingRate, true);
                {
                    std::lock_guard<std::mutex> lock(dataMutex);
                    for (int i = 0; i < 5; i++) {
                        currentBandPowers[i] = bands.first[i];
                    }
                }
                delete[] bands.first;
                delete[] bands.second;
            }
        }
        
        TrySSVEPDetect(enabled);
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow Processing Error] " << err.what() << std::endl;
    }
}

EEGPlotSnapshot GanglionHandler::GetEEGPlotSnapshot() {
    EEGPlotSnapshot snap;
    int fs = 200;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        fs = samplingRate > 0 ? samplingRate : 200;
        snap.samplingRate = fs;
        for (int ch = 0; ch < 4; ++ch) {
            const EEGRing& ring = eegRings[ch];
            snap.hadData[ch] = ring.hadData;
            snap.count[ch] = ring.size;
            const int start = (ring.size == kEEGPlotCapacity) ? ring.head : 0;
            for (int i = 0; i < ring.size; ++i) {
                const int idx = (start + i) % kEEGPlotCapacity;
                snap.t[ch][i] = ring.t[idx];
                snap.y[ch][i] = ring.y[idx];
            }
        }
    }

    // Causal Butterworth restarts at rest on whatever window we pass in, so the
    // leftmost samples always show a settling spike. Drop that warmup; the ring
    // keeps extra raw history so the visible window still slides ~5 s.
    const int warmup = std::max(kPlotFilterMinSamples, fs * 2); // 
    const int minKeep = std::max(kPlotFilterMinSamples, fs);    // keep >= ~1 s

    for (int ch = 0; ch < 4; ++ch) {
        const int n = snap.count[ch];
        ApplySSVEPPlotFilters(snap.y[ch].data(), n, fs);

        int skip = 0;
        if (n > minKeep) {
            skip = std::min(warmup, n - minKeep);
        }
        if (skip > 0) {
            const int kept = n - skip;
            std::memmove(snap.t[ch].data(), snap.t[ch].data() + skip,
                static_cast<size_t>(kept) * sizeof(float));
            std::memmove(snap.y[ch].data(), snap.y[ch].data() + skip,
                static_cast<size_t>(kept) * sizeof(float));
            snap.count[ch] = kept;
        }
    }
    return snap;
}

void GanglionHandler::SetMonitorRefreshHz(int hz) {
    if (hz >= 30) monitorRefreshHz = hz;
}

SSVEPDetectSnapshot GanglionHandler::GetSSVEPSnapshot() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return ssvepSnapshot;
}

void GanglionHandler::TrySSVEPDetect(const bool enabled[4]) {
    const int fs = samplingRate > 0 ? samplingRate : 200;
    int windowN = static_cast<int>(std::lround(currentWindowSec * fs));
    if (windowN % 2) windowN--;
    windowN = std::max(32, windowN);
    const int hopN = std::max(1, static_cast<int>(std::lround(currentWindowSec * fs)));

    bool maskChanged = false;
    int shortest = windowN;
    bool any = false;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        for (int ch = 0; ch < 4; ++ch) {
            if (enabled[ch] != lastDetectEnabled[ch]) maskChanged = true;
            lastDetectEnabled[ch] = enabled[ch];
            if (!enabled[ch]) continue;
            any = true;
            shortest = std::min(shortest, eegRings[ch].size);
        }
    }
    if (maskChanged) {
        ssvepDetector.Reset();
        lastDetectSample = sampleIndex;
    }
    ssvepDetector.SetRefreshHz(monitorRefreshHz);
    ssvepDetector.NoteProgress(static_cast<float>(shortest) / static_cast<float>(fs), fs);

    if (!any || shortest < windowN) {
        std::lock_guard<std::mutex> lock(dataMutex);
        ssvepSnapshot = ssvepDetector.Snapshot();
        return;
    }
    if (sampleIndex >= lastDetectSample
        && sampleIndex - lastDetectSample < static_cast<std::uint64_t>(hopN)
        && ssvepDetector.Snapshot().ready) {
        return;
    }
    if (sampleIndex > lastDetectSample
        && sampleIndex - lastDetectSample > static_cast<std::uint64_t>(windowN)) {
        ssvepDetector.Reset();
        ssvepDetector.SetRefreshHz(monitorRefreshHz);
    }

    std::vector<float> storage[4];
    const float* ptrs[4] = {};
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        for (int ch = 0; ch < 4; ++ch) {
            if (!enabled[ch]) continue;
            const EEGRing& ring = eegRings[ch];
            storage[ch].resize(static_cast<size_t>(windowN));
            const int start = (ring.size == kEEGPlotCapacity) ? ring.head : 0;
            const int first = ring.size - windowN;
            for (int i = 0; i < windowN; ++i) {
                const int idx = (start + first + i) % kEEGPlotCapacity;
                storage[ch][static_cast<size_t>(i)] = ring.y[idx];
            }
            ptrs[ch] = storage[ch].data();
        }
    }

    ssvepDetector.Analyze(ptrs, enabled, windowN, fs);
    lastDetectSample = sampleIndex;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        ssvepSnapshot = ssvepDetector.Snapshot();
    }
}

bool GanglionHandler::StartImpedanceMode(const std::string& port, const std::string& macAddress) {
    lastConnectError.clear();
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
        lastConnectError = err.what() ? err.what() : "Unknown BrainFlow error";
        std::cerr << "[BrainFlow] Failed to start impedance check: " << lastConnectError << std::endl;
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

std::string GanglionHandler::LastConnectError() const {
    return lastConnectError;
}

std::vector<double> GanglionHandler::GetBandPowers() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return currentBandPowers;
}
