#pragma once

#include <array>
#include <cstdint>

static constexpr int kSSVEPSpectrumCapacity = 512;
static constexpr int kSSVEPVoteLength = 4;
static constexpr double kSSVEPWindowSec = 5.0;
static constexpr double kSSVEPHopSec = 5.0;

struct SSVEPDetectSnapshot {
    bool ready = false;
    int refreshHz = 60;
    int samplingRate = 200;
    float targetHz[4] = { 5.0f, 9.0f, 13.0f, 17.0f };
    float corr[4] = {};
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
    float gateRatio[4] = {};
};

// Monopoly-style CCA (2 harmonics) on a 5 s window, majority of the last 4
// hops (5 s). A full vote covers ~20 s, not a 10 s trial.
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
