#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#include "SSVEPTargets.h"
#include "HudOverlay.h"

struct GLFWwindow;

struct SSVEPCalibrationTrial {
    bool complete = false;
    int samplingRate = 200;
    std::vector<std::array<float, 4>> samples;
};

// One countdown, then 5 s of the selected board. Calibrate All is the same
// session walked across every target, with a separate trial buffer each.
class CalibrationSession {
public:
    void Start(int board, const SSVEPBoardLayout& layout, GLFWwindow* owner, float dpi,
        unsigned patternTex, unsigned patternInvTex);
    void StartAll(const SSVEPBoardLayout& layout, GLFWwindow* owner, float dpi,
        unsigned patternTex, unsigned patternInvTex);
    void Cancel();
    void Shutdown();

    void Update(double nowSec);
    void Render();

    bool IsActive() const { return active_; }
    bool IsRecording() const;
    void PushSample(const float channels[4], int samplingRate);

    const SSVEPCalibrationTrial& Trial(int board) const;
    void CopyTrial(int board, SSVEPCalibrationTrial& out) const;
    bool HasTrial(int board) const;
    void ClearTrial(int board);

private:
    void Begin(const std::vector<int>& boards, const SSVEPBoardLayout& layout,
        GLFWwindow* owner, float dpi, unsigned patternTex, unsigned patternInvTex);
    void OpenWindow();
    void CloseWindow();
    void CommitCurrent();
    void Advance(double nowSec);
    void DrawBoard(float cx, float cy, float side);
    void DrawLabel(const char* text, float cx, float cy, float maxW, float maxH, float r, float g, float b);
    void DrawCancel(int winW, int winH);
    bool CancelHit(double mx, double my, int winW, int winH) const;

    bool active_ = false;
    bool recording_ = false;
    bool countdown_ = true;
    double phaseStart_ = 0.0;
    int queueIndex_ = 0;
    std::vector<int> queue_;

    SSVEPBoardLayout layout_{};
    GLFWwindow* owner_ = nullptr;
    GLFWwindow* window_ = nullptr;
    float dpi_ = 1.0f;
    unsigned patternTex_ = 0;
    unsigned patternInvTex_ = 0;

    unsigned program_ = 0;
    unsigned vao_ = 0;
    unsigned vbo_ = 0;
    unsigned fontTex_ = 0;
    int fontAtlas_ = 512;
    void* fontChars_ = nullptr;
    unsigned digitTex_ = 0;
    int digitAtlas_ = 0;
    void* digitChars_ = nullptr;
    void* arrowChars_ = nullptr;
    bool arrowReady_[4] = {};
    unsigned keyTex_[5] = {};
    int keyW_[5] = {};
    int keyH_[5] = {};
    bool mouseDown_ = false;
    bool escapeDown_ = false;

    SSVEPCalibrationTrial stored_[kSSVEPBoardCount];
    std::vector<std::array<float, 4>> scratchSamples_;
    int scratchRate_ = 200;
    mutable std::mutex mutex_;
};
