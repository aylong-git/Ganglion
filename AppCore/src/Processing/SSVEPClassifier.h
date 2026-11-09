#pragma once

#include <array>
#include <cstdint>
#include "SSVEPTargets.h"

static constexpr int kSSVEPSpectrumCapacity = 512;
static constexpr int kSSVEPVoteLength = 4;
static constexpr double kSSVEPWindowSec = 5.0;
static constexpr double kSSVEPHopSec = 5.0;

struct SSVEPDetectSnapshot {
    bool ready = false;
    int refreshHz = 60;
    int samplingRate = 200;
    float targetHz[4] = {
        kSSVEPTargetHz[0],
        kSSVEPTargetHz[1],
        kSSVEPTargetHz[2],
        kSSVEPTargetHz[3]
    };
    int windowWinner = -1;
    int votedWinner = -1;
    int votesCast = 0;
    int votesForWinner = 0;
    int spectrumCount = 0;
    std::array<float, kSSVEPSpectrumCapacity> freq{};
    std::array<float, kSSVEPSpectrumCapacity> power{};
    float secondsCollected = 0.0f;
    float windowSec = 5.0f;
    float hopSec = 5.0f;
    float corr[4] = {};
    float harmonicSnr[4] = {};
    float appliedSnr[4] = {};
    bool snrGate[4] = {};
    float gateRatio = 0.0f;
    // Increments once each time a detection window finishes.
    std::uint32_t epoch = 0;
};

// Hann SNR at each target: channel weight * (SNR(f) + 0.5 * SNR(2f)).
// A null channel pointer is skipped. Power is the squared one-sided spectrum.
bool MeasureHarmonicSnr(const float* const channels[4], int nSamples, int fs,
    const float targetHz[4], float outSnr[4]);
// Power at the Hann bin nearest targetHz, the highest of the four channels.
// Same 4–40 Hz bandpass the calibration plot uses, so the guide line
// sits on the filtered spectrum. The saved CSV stays unfiltered.
float HannPeakPower(const float* const channels[4], int nSamples, int fs, float targetHz);
void ApplyDisplaySpectrumFilter(double* data, int n, int fs, double lowHz = 4.0, double highHz = 40.0);

// Filter-bank CCA (Chen 2015, M3) on one window. The caller rejects the
// winner when its Hann SNR is at or below that board's calibration cutoff.
class SSVEPDetector {
public:
    void Reset();
    void SetRefreshHz(int hz);
    void SetTargetFrequencies(const float hz[4]);
    void SetWindowConfig(float windowSeconds, float hopSeconds);
    void NoteProgress(float secondsCollected, int fs);
    void Analyze(const float* const channels[4], const bool enabled[4], int nSamples, int fs);
    SSVEPDetectSnapshot Snapshot() const { return snap; }
    

private:
    int refreshHz = 60;
    float windowSec = 5.0f;
    float hopSec = 5.0f;
    int voteTarget[kSSVEPVoteLength] = {};
    float voteScore[kSSVEPVoteLength] = {};
    int voteFilled = 0;
    int voteWrite = 0;
    SSVEPDetectSnapshot snap{};

    void PushVote(int target, float score);
    void ResolveVote(int& winner, int& votesFor, int& cast) const;
};
