#pragma once
#include <map>
#include <atomic>
#include <string>
#include <vector>
#include <array>
#include <mutex>
#include <cstdint>
#include <GLFW/glfw3.h>
#include "HeadObject.h"
#include "HudOverlay.h"
#include "GanglionHandler.h"
#include "SSVEPClassifier.h"

struct ImFont;

const float SSVEP_Hz[] = { 13.0f, 15.5f, 17.0f, 23.5f };

class UIManager {
public:
    UIManager();
    ~UIManager();

    HeadObject headObject;
    HudOverlay hudOverlay;
    bool Initialize();
    void Run(std::atomic<bool>& isRunning);
    void Cleanup();
    ImVec4 ColorConvertU32ToFloat4(ImU32 in);

    // Connection Panel
    char macParts[6][3] = { "", "", "", "", "", "" };
    char macAddress[18] = "";
    unsigned int macHelpTextureID = 0;

    unsigned int qrCodeTextureID = 0;
    unsigned int qrCodeInvTextureID = 0;
    int SSVEPPatternMode = 1;
    bool SSVEPContrastMode = true;
    bool SSVEPSmoothMode = true;
    int selectedPortIdx = 0;
    std::vector<std::string> availablePorts;
    bool isConnected = false;
    void UpdateAvailablePorts();

    bool showPortErrorPopup = false;
    std::string portErrorMessage = "";

    // Asynchronous flags
    bool reqConnect = false;
    bool reqDisconnect = false;

    // Control Settings
    bool eegChannels[4] = { true, true, true, true };
    float statusRefreshTime = 0.1f;

    // Band Power Arrays
    bool selectedWaves[5] = { true, true, true, true, true };
    float currentBandPowers[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    // Live EEG traces (filled by the worker thread)
    std::mutex eegPlotMutex;
    EEGPlotSnapshot eegPlot;
    SSVEPDetectSnapshot SSVEPDetect;
    std::atomic<bool> signalTabActive{ false };
    std::atomic<int> monitorRefreshHz{ 60 };
    std::uint64_t SSVEPFrame = 0;

    // EEG CSV recording
    std::atomic<bool> isRecording{ false };
    bool reqStartRecord = false;
    bool reqStopRecord = false;
    bool showRecordPanel = false;
    bool showRecordErrorPopup = false;
    std::string recordErrorMessage;
    char recordFileName[256] = "";
    char recordDirectory[512] = "";
    std::atomic<bool> isFolderDialogWaiting{ false };
    std::string GetFullRecordPath() const;

    // Impedance Tab
    bool isCheckingImpedance = false;
    std::array<float, 5> currentImpedances = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool reqImpedanceCheck = false;
    bool reqStopImpedanceCheck = false;
    float m_dpiScale = 1.0f;
    int m_swapInterval = 1;
    float SSVEPWindowDuration = 5.0f;
    bool SSVEPPreviewEnabled = false;
    bool SSVEPTestMode = false;
    int SSVEPTestSeq[12] = {};
    int SSVEPTestSeqLen = 0;
    int SSVEPTestIndex = 0;
    int SSVEPTestCorrect = 0;
    bool SSVEPTestDone = false;
    bool SSVEPTestHit[12] = {};
    ImFont* SSVEPKeyFont = nullptr;
    SSVEPBoardLayout SSVEPBoardLayout;
    bool SSVEPBoardLayoutInit = false;

private:
    GLFWwindow* window;
    std::map<int, ImFont*> fonts;
    int m_preTestX = 0;
    int m_preTestY = 0;
    int m_preTestW = 1280;
    int m_preTestH = 720;
    bool m_wasIconified = false;
    bool m_recenterRecordPanel = false;

    void SetupCustomTheme();
    void RenderUI();
    void DrawSignalEEGPanel();
    void DrawSignalSpectrumPanel();
    void DrawRecordConfigPopup();
    void FillDefaultRecordName();
};
