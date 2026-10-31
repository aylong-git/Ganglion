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
    constexpr int kPlotFilterMinSamples = 24;
    // BrainFlow treats 0 as "use the default", and the Ganglion default is 15 s
    // for both the BLE search and the wait for the first sample.
    constexpr int kGanglionConnectTimeoutSec = 5;

    BrainFlowInputParams MakeGanglionParams(const std::string& port, const std::string& mac) {
        BrainFlowInputParams params;
        params.timeout = kGanglionConnectTimeoutSec;
        // Empty other_info makes BrainFlow decode every packet as firmware 3,
        // and it ignores the version the dongle actually reports. A v2 board
        // then stays near 0 uV. fw:auto uses the detected version.
        params.other_info = "fw:auto";
        if (!port.empty()) params.serial_port = port;
        if (!mac.empty()) params.mac_address = mac;
        return params;
    }

    // Same length that used to be a flat 2 s, which matched a ~3 Hz lower edge
    // (order / 3 Hz). A 3 Hz board drops that edge to 1 Hz, so the tail is
    // longer and must be scaled or it stays glued to the left of the plot.
    int PlotFilterWarmupSamples(int fs, double lowHz)
    {
        const double flo = std::max(lowHz, 0.5);
        const double seconds = static_cast<double>(kEEGFilterOrder) / flo;
        const int n = static_cast<int>(std::ceil(seconds * static_cast<double>(fs)));
        return std::max(kPlotFilterMinSamples, n);
    }
}

void GanglionHandler::SetSSVEPWindow(float windowSeconds) {
    std::lock_guard<std::mutex> lock(dataMutex);
    currentWindowSec = windowSeconds;
    // For your immediate goal, hop ratio equals window ratio (no stride/overlap)
    ssvepDetector.SetWindowConfig(windowSeconds, windowSeconds); 
}

void GanglionHandler::SetTargetFrequencies(const float hz[4]) {
    std::lock_guard<std::mutex> lock(dataMutex);
    
    for (int i = 0; i < 4; ++i) {
        currentTargetHz[i] = hz[i];
    }
    
    // Lowest board frequency minus 2 Hz, through the second harmonic of the highest plus 2 Hz.
    plotBandLowHz = SSVEPPlotBandLowHz(hz);
    plotBandHighHz = SSVEPPlotBandHighHz(hz);
    
    // Forward the frequencies to the CCA detector
    ssvepDetector.SetTargetFrequencies(hz);
}

void GanglionHandler::ApplySSVEPPlotFilters(float* y, int n, int samplingRate, double lowHz, double highHz) {
    if (y == nullptr || n < kPlotFilterMinSamples || samplingRate <= 0) return;
    if (!(highHz > lowHz)) return;

    std::vector<double> buf(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        buf[static_cast<size_t>(i)] = static_cast<double>(y[i]);
    }

    try {
        // Use the dynamically calculated bounds
        DataFilter::perform_bandpass(
            buf.data(), n, samplingRate, lowHz, highHz,
            kEEGFilterOrder, (int)FilterTypes::BUTTERWORTH, 0.0);
        DataFilter::perform_bandstop(
            buf.data(), n, samplingRate, 48.0, 52.0,
            kEEGFilterOrder, (int)FilterTypes::BUTTERWORTH, 0.0);
        DataFilter::perform_bandstop(
            buf.data(), n, samplingRate, 58.0, 62.0,
            kEEGFilterOrder, (int)FilterTypes::BUTTERWORTH, 0.0);
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

    // A failed attempt can leave GanglionLib holding the COM port. Drop it
    // before opening again, or the replugged dongle is still busy.
    if (board != nullptr) {
        Disconnect();
    }
    else {
        try {
            BoardShim::release_all_sessions();
        }
        catch (...) {}
    }

    try {
        BoardShim::enable_dev_board_logger();
        std::cout << "[BrainFlow] Connecting port='" << port << "' mac='" << macAddress << "'\n";

        struct BrainFlowInputParams params = MakeGanglionParams(port, macAddress);

        boardId = (int)BoardIds::GANGLION_BOARD;
        samplingRate = BoardShim::get_sampling_rate(boardId);
        board = new BoardShim(boardId, params);

        if (!board->is_prepared()) {
            board->prepare_session();
        }

        // Buffer only. An empty streamer string does not write a file.
        board->start_stream(450000, "");
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
        // stop_stream throws when the read thread has already exited. That used
        // to skip release_session. BoardShim's destructor does not release
        // either, so the native Ganglion session, the open COM port, and
        // GanglionLib's process-lifetime state stayed alive. The next connect
        // could not replace that session; only exiting the app could.
        try {
            if (board->is_prepared()) {
                try {
                    board->stop_stream();
                }
                catch (const BrainFlowException& err) {
                    std::cerr << "[BrainFlow] stop_stream: " << err.what() << "\n";
                }
                try {
                    board->release_session();
                }
                catch (const BrainFlowException& err) {
                    std::cerr << "[BrainFlow] release_session: " << err.what() << "\n";
                }
            }
        }
        catch (const BrainFlowException& err) {
            std::cerr << "[BrainFlow] is_prepared: " << err.what() << "\n";
        }
        catch (...) {}
        delete board;
        board = nullptr;
    }

    try {
        BoardShim::release_all_sessions();
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow] release_all_sessions: " << err.what() << "\n";
    }
    catch (...) {}

    {
        std::lock_guard<std::mutex> lock(dataMutex);
        ClearEEGRingsLocked();
    }
    connected = false;
    impedanceMode = false;
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
    double lowHz = plotBandLowHz;
    double highHz = plotBandHighHz;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        fs = samplingRate > 0 ? samplingRate : 200;
        lowHz = plotBandLowHz;
        highHz = plotBandHighHz;
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
    // leftmost samples always show a settling wave. That tail gets longer as
    // the lower cutoff drops, so the discarded prefix scales with it. Then
    // keep only the latest display window.
    const int warmup = std::min(kEEGFilterWarmupCapacity, PlotFilterWarmupSamples(fs, lowHz));
    const int minKeep = std::max(kPlotFilterMinSamples, fs);    // keep >= ~1 s

    for (int ch = 0; ch < 4; ++ch) {
        const int n = snap.count[ch];
        ApplySSVEPPlotFilters(snap.y[ch].data(), n, fs, lowHz, highHz);

        int skip = 0;
        if (n > minKeep) {
            skip = std::min(warmup, n - minKeep);
        }
        if (n - skip > kEEGPlotDisplayCapacity) {
            skip = n - kEEGPlotDisplayCapacity;
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
    if (board != nullptr) {
        Disconnect();
    }
    else {
        try {
            BoardShim::release_all_sessions();
        }
        catch (...) {}
    }

    struct BrainFlowInputParams params = MakeGanglionParams(port, macAddress);

    boardId = (int)BoardIds::GANGLION_BOARD;
    samplingRate = BoardShim::get_sampling_rate(boardId);
    board = new BoardShim(boardId, params);

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
        Disconnect();
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
