#pragma once

#include <array>
#include <cstdint>

static constexpr int kSSVEPSpectrumCapacity = 512;
static constexpr int kSSVEPVoteLength = 4;
static constexpr double kSSVEPWindowSec = 2.0;
static constexpr double kSSVEPHopSec = 0.5;

struct SSVEPDetectSnapshot {
    bool ready = false;
    int refreshHz = 60;
    int samplingRate = 200;
    float targetHz[4] = { 10.0f, 12.0f, 15.0f, 20.0f };
    float corr[4] = {};
    int windowWinner = -1;
    int votedWinner = -1;
    int votesCast = 0;
    int votesForWinner = 0;
    int spectrumCount = 0;
    std::array<float, kSSVEPSpectrumCapacity> freq{};
    std::array<float, kSSVEPSpectrumCapacity> power{};
    float secondsCollected = 0.0f;
    float windowSec = static_cast<float>(kSSVEPWindowSec);
};

// Monopoly-style CCA (2 harmonics) on a 2 s window, majority of the last 4
// hops (0.5 s). A full vote covers ~3.5 s, not a 10 s trial.
class SSVEPDetector {
public:
    void Reset();
    void SetRefreshHz(int hz);
    void SetTargetFrequencies(const float hz[4]);
    void NoteProgress(float secondsCollected, int fs);
    void Analyze(const float* const channels[4], const bool enabled[4], int nSamples, int fs);
    SSVEPDetectSnapshot Snapshot() const { return snap; }
    

private:
    int refreshHz = 60;
    int voteTarget[kSSVEPVoteLength] = {};
    float voteScore[kSSVEPVoteLength] = {};
    int voteFilled = 0;
    int voteWrite = 0;
    SSVEPDetectSnapshot snap{};

    void PushVote(int target, float score);
    void ResolveVote(int& winner, int& votesFor, int& cast) const;
};
