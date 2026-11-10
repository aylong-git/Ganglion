#include "GanglionHandler.h"
#include "EEGRecorder.h"
#include "Calibration.h"
#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include "data_filter.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
    constexpr int kPlotFilterMinSamples = 24;
    // BrainFlow treats 0 as "use the default", and the Ganglion default is 15 s
    // for both the BLE search and the wait for the first sample.
    constexpr int kGanglionConnectTimeoutSec = 5;

#if defined(_WIN32)
    // close_ganglion closes the COM handle. If that happens before the BLED112
    // sends a BLE disconnect, the Ganglion blue LED stays solid. Reopen the
    // port and send BGAPI system_reset so the dongle drops the link.
    void ResetBled112(const std::string& port)
    {
        if (port.empty() || port == "No Dongle Found") return;
        std::string path = port;
        if (path.rfind("\\\\.\\", 0) != 0) path = "\\\\.\\" + port;

        HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            std::cerr << "[BrainFlow] Could not reopen " << port << " to drop the BLE link.\n";
            return;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle, &dcb)) {
            CloseHandle(handle);
            return;
        }
        dcb.BaudRate = 256000;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        if (!SetCommState(handle, &dcb)) {
            CloseHandle(handle);
            return;
        }

        COMMTIMEOUTS timeouts{};
        timeouts.WriteTotalTimeoutConstant = 500;
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 200;
        SetCommTimeouts(handle, &timeouts);

        const unsigned char reset[5] = { 0x00, 0x01, 0x00, 0x00, 0x00 };
        DWORD wrote = 0;
        WriteFile(handle, reset, sizeof(reset), &wrote, nullptr);
        FlushFileBuffers(handle);
        Sleep(1000);
        CloseHandle(handle);
    }
#endif

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

    void PlayDetectionChime()
    {
#if defined(_WIN32)
        // PlaySound only accepts PCM. This clip is 32-bit float, so convert once.
        static std::vector<char> pcm;
        static bool tried = false;
        if (!tried) {
            tried = true;
            std::ifstream in(GetAssetPath("hitfinish.wav"), std::ios::binary);
            std::vector<unsigned char> file;
            if (in) {
                in.seekg(0, std::ios::end);
                const std::streamoff n = in.tellg();
                in.seekg(0, std::ios::beg);
                if (n > 44 && n < 8 * 1024 * 1024) {
                    file.resize(static_cast<size_t>(n));
                    in.read(reinterpret_cast<char*>(file.data()), n);
                }
            }
            auto tagAt = [&](size_t at) -> unsigned {
                if (at + 4 > file.size()) return 0;
                return static_cast<unsigned>(file[at])
                    | (static_cast<unsigned>(file[at + 1]) << 8)
                    | (static_cast<unsigned>(file[at + 2]) << 16)
                    | (static_cast<unsigned>(file[at + 3]) << 24);
            };
            if (file.size() > 44 && tagAt(0) == 0x46464952 && tagAt(8) == 0x45564157) {
                size_t at = 12;
                int channels = 0;
                int rate = 0;
                int bits = 0;
                int format = 0;
                const unsigned char* data = nullptr;
                unsigned dataBytes = 0;
                while (at + 8 <= file.size()) {
                    const unsigned id = tagAt(at);
                    const unsigned sz = tagAt(at + 4);
                    const size_t body = at + 8;
                    if (body + sz > file.size()) break;
                    if (id == 0x20746d66 && sz >= 16) {
                        format = file[body] | (file[body + 1] << 8);
                        channels = file[body + 2] | (file[body + 3] << 8);
                        rate = static_cast<int>(tagAt(body + 4));
                        bits = file[body + 14] | (file[body + 15] << 8);
                    }
                    else if (id == 0x61746164) {
                        data = file.data() + body;
                        dataBytes = sz;
                    }
                    at = body + sz + (sz & 1u);
                }
                const bool floatStereo = format == 3 && channels == 2 && bits == 32 && rate > 0 && data != nullptr;
                const unsigned frameBytes = 8;
                if (floatStereo && dataBytes >= frameBytes && (dataBytes / frameBytes) < 96000u * 5u) {
                    const unsigned frames = dataBytes / frameBytes;
                    const unsigned pcmBytes = frames * 4;
                    pcm.resize(44 + pcmBytes);
                    auto put32 = [&](size_t o, unsigned v) {
                        pcm[o] = static_cast<char>(v & 0xff);
                        pcm[o + 1] = static_cast<char>((v >> 8) & 0xff);
                        pcm[o + 2] = static_cast<char>((v >> 16) & 0xff);
                        pcm[o + 3] = static_cast<char>((v >> 24) & 0xff);
                    };
                    auto put16 = [&](size_t o, unsigned v) {
                        pcm[o] = static_cast<char>(v & 0xff);
                        pcm[o + 1] = static_cast<char>((v >> 8) & 0xff);
                    };
                    std::memcpy(pcm.data(), "RIFF", 4);
                    put32(4, 36 + pcmBytes);
                    std::memcpy(pcm.data() + 8, "WAVEfmt ", 8);
                    put32(16, 16);
                    put16(20, 1);
                    put16(22, 2);
                    put32(24, static_cast<unsigned>(rate));
                    put32(28, static_cast<unsigned>(rate) * 4);
                    put16(32, 4);
                    put16(34, 16);
                    std::memcpy(pcm.data() + 36, "data", 4);
                    put32(40, pcmBytes);
                    for (unsigned i = 0; i < frames * 2; ++i) {
                        float sample = 0.0f;
                        std::memcpy(&sample, data + i * 4, 4);
                        if (sample > 1.0f) sample = 1.0f;
                        if (sample < -1.0f) sample = -1.0f;
                        const int v = static_cast<int>(sample * 32767.0f);
                        put16(44 + i * 2, static_cast<unsigned>(v));
                    }
                }
            }
        }
        if (!pcm.empty()) {
            PlaySoundA(pcm.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
#else
        (void)0;
#endif
    }

    void ApplyCalibrationSnrGate(CalibrationSession* session, SSVEPDetectSnapshot& snap) {
        if (!session || snap.windowWinner < 0) return;
        for (int i = 0; i < kSSVEPFlickerBoardCount; ++i) {
            SSVEPBoardThreshold threshold;
            if (session->CopySnrThreshold(i, threshold)) {
                snap.snrGate[i] = true;
                snap.appliedSnr[i] = threshold.appliedSnr;
            }
            else {
                snap.snrGate[i] = false;
                snap.appliedSnr[i] = 0.0f;
            }
        }
        const int choice = snap.windowWinner;
        if (!snap.snrGate[choice]) {
            snap.votedWinner = choice;
            return;
        }
        const bool pass = snap.harmonicSnr[choice] > snap.appliedSnr[choice];
        snap.votedWinner = pass ? choice : -1;
        snap.gateRatio = (snap.appliedSnr[choice] > 0.0f)
            ? (snap.harmonicSnr[choice] / snap.appliedSnr[choice]) : 0.0f;
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

// Firmware 2 scale times the 24-bit code -2^23. A live electrode does not sit
// on this exact value with no noise; it is the MCP3912 reset code, repeated
// because the ADC never left reset. The plot bandpass then turns that
// constant into 0 uV after a short startup ring.
bool GanglionStreamStuckAtRail(BoardShim* board, int boardId) {
    if (board == nullptr) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    BrainFlowArray<double, 2> data = board->get_board_data();
    const int n = data.get_size(1);
    if (n < 40) return false;

    std::vector<int> eeg;
    try {
        eeg = BoardShim::get_eeg_channels(boardId);
    }
    catch (...) {
        return false;
    }
    if (static_cast<int>(eeg.size()) < 4) return false;

    for (int ch = 0; ch < 4; ++ch) {
        const int row = eeg[static_cast<size_t>(ch)];
        if (row < 0 || row >= data.get_size(0)) return false;
        const double* col = data.get_address(row);
        double lo = col[0];
        double hi = col[0];
        for (int i = 1; i < n; ++i) {
            lo = std::min(lo, col[i]);
            hi = std::max(hi, col[i]);
        }
        if (hi - lo > 1.0) return false;
        if (std::fabs(0.5 * (lo + hi) + 15686.3) > 5.0) return false;
    }
    return true;
}

bool GanglionHandler::Connect(const std::string& port, const std::string& macAddress) {
    if (connected) return true;
    lastConnectError.clear();
    serialPort = port;

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
        if (GanglionStreamStuckAtRail(board, boardId)) {
            // 'v' reinitializes the MCP3912. BrainFlow stops the stream, sends
            // the command, then starts 'b' again.
            std::cerr << "[BrainFlow] EEG is stuck at the full-scale code -15686 uV. Resetting the ADC.\n";
            try {
                board->config_board("v");
            }
            catch (const BrainFlowException& err) {
                std::cerr << "[BrainFlow] Peripheral reset failed: " << err.what() << "\n";
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            bool stillStuck = true;
            try {
                board->get_board_data();
                stillStuck = GanglionStreamStuckAtRail(board, boardId);
            }
            catch (const BrainFlowException& err) {
                std::cerr << "[BrainFlow] Stream did not resume after reset: " << err.what() << "\n";
                stillStuck = true;
            }
            if (stillStuck) {
                lastConnectError = "Ganglion ADC is stuck at full scale. Switch the board off, wait a few seconds, switch it on, then connect again.";
                std::cerr << "[BrainFlow] " << lastConnectError << "\n";
                Disconnect();
                return false;
            }
            std::cout << "[BrainFlow] ADC reset recovered the EEG stream.\n";
        }
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
    const bool hadSession = board != nullptr || connected.load() || impedanceMode.load();
    if (!hadSession) return;

    const std::string port = serialPort;
    if (board != nullptr) {
        // release_session stops the stream and then calls close_ganglion.
        // A separate stop_stream first joins the reader and sends "s", and
        // close_ganglion then closes the COM port before the BLED112 transmits
        // the BLE disconnect. The Ganglion LED stays solid.
        try {
            board->release_session();
        }
        catch (const BrainFlowException& err) {
            std::cerr << "[BrainFlow] release_session: " << err.what() << "\n";
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

#if defined(_WIN32)
    ResetBled112(port);
#endif

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

void GanglionHandler::SetCalibrationSession(CalibrationSession* session) {
    calibrationSession = session;
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
                if (calibrationSession && calibrationSession->IsRecording()) {
                    float row[4] = {};
                    for (int ch = 0; ch < 4; ++ch) {
                        if (ch < static_cast<int>(allEEGChannels.size())) {
                            row[ch] = static_cast<float>(data.get_address(allEEGChannels[ch])[s]);
                        }
                    }
                    calibrationSession->PushSample(row, samplingRate);
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

    const int liveN = std::min(kEEGLiveSpectrumCapacity, std::max(1, fs * 9));
    for (int ch = 0; ch < 4; ++ch) {
        const int n = snap.count[ch];
        const int take = std::min(n, liveN);
        snap.liveCount[ch] = take;
        if (take > 0) {
            std::memcpy(snap.liveY[ch].data(), snap.y[ch].data() + (n - take),
                static_cast<size_t>(take) * sizeof(float));
        }

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
    const int pauseN = std::max(1, fs * 2);
    const int hopN = windowN + pauseN;

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

    auto publishPause = [&]() {
        bool paused = false;
        if (ssvepDetector.Snapshot().ready && sampleIndex >= lastDetectSample) {
            paused = (sampleIndex - lastDetectSample) < static_cast<std::uint64_t>(pauseN);
        }
        flickerPaused.store(paused);
    };

    if (!any || shortest < windowN) {
        std::lock_guard<std::mutex> lock(dataMutex);
        ssvepSnapshot = ssvepDetector.Snapshot();
        publishPause();
        return;
    }
    if (sampleIndex >= lastDetectSample
        && sampleIndex - lastDetectSample < static_cast<std::uint64_t>(hopN)
        && ssvepDetector.Snapshot().ready) {
        std::lock_guard<std::mutex> lock(dataMutex);
        ssvepSnapshot = ssvepDetector.Snapshot();
        ApplyCalibrationSnrGate(calibrationSession, ssvepSnapshot);
        publishPause();
        return;
    }
    if (sampleIndex > lastDetectSample
        && sampleIndex - lastDetectSample > static_cast<std::uint64_t>(hopN + windowN)) {
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

    float channelWeight[4] = { 0.20f, 0.30f, 0.30f, 0.20f };
    if (calibrationSession) calibrationSession->CopyChannelWeights(channelWeight);
    ssvepDetector.SetChannelWeights(channelWeight);
    ssvepDetector.Analyze(ptrs, enabled, windowN, fs);
    lastDetectSample = sampleIndex;
    if (ssvepDetector.Snapshot().ready) PlayDetectionChime();
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        ssvepSnapshot = ssvepDetector.Snapshot();
        ApplyCalibrationSnrGate(calibrationSession, ssvepSnapshot);
    }
    publishPause();
}

bool GanglionHandler::StartImpedanceMode(const std::string& port, const std::string& macAddress) {
    lastConnectError.clear();
    serialPort = port;
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

    // "Z" only leaves impedance mode. It does not drop BLE. Disconnect()
    // releases the session and resets the dongle so the blue LED blinks.
    try {
        board->config_board("Z");
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[BrainFlow] Failed to leave impedance mode: " << err.what() << std::endl;
    }
    catch (...) {}
    Disconnect();
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
