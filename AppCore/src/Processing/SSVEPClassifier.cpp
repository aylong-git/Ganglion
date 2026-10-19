#include "SSVEPClassifier.h"
#include "SSVEPTargets.h"

#include "data_filter.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr int kHarmonics = 2;
constexpr double kPi = 3.14159265358979323846;

double CanonicalCorrelation(const Eigen::MatrixXd& X, const Eigen::MatrixXd& Y) {
    const int n = static_cast<int>(X.rows());
    if (n < 8 || X.cols() < 1 || Y.cols() < 1) return 0.0;

    Eigen::MatrixXd Xc = X.rowwise() - X.colwise().mean();
    Eigen::MatrixXd Yc = Y.rowwise() - Y.colwise().mean();
    for (int c = 0; c < Yc.cols(); ++c) {
        const double sd = std::sqrt(Yc.col(c).squaredNorm() / static_cast<double>(n));
        Yc.col(c) /= (sd < 1e-12) ? 1.0 : sd;
    }

    Eigen::MatrixXd Cxx = Xc.transpose() * Xc;
    Eigen::MatrixXd Cyy = Yc.transpose() * Yc;
    const Eigen::MatrixXd Cxy = Xc.transpose() * Yc;
    const double ridge = 1e-6 * static_cast<double>(n);
    Cxx.diagonal().array() += ridge;
    Cyy.diagonal().array() += ridge;

    Eigen::LLT<Eigen::MatrixXd> lltX(Cxx);
    Eigen::LLT<Eigen::MatrixXd> lltY(Cyy);
    if (lltX.info() != Eigen::Success || lltY.info() != Eigen::Success) return 0.0;

    Eigen::MatrixXd M = lltX.matrixL().solve(Cxy);
    M = lltY.matrixL().solve(M.transpose()).transpose();
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(M);
    if (svd.singularValues().size() < 1) return 0.0;
    return std::min(1.0, std::abs(svd.singularValues()(0)));
}

Eigen::MatrixXd ReferenceMatrix(double freqHz, int nSamples, int fs) {
    Eigen::MatrixXd X(nSamples, kHarmonics * 2);
    const double dt = 1.0 / static_cast<double>(fs);
    for (int i = 0; i < nSamples; ++i) {
        const double t = static_cast<double>(i) * dt;
        int col = 0;
        for (int h = 1; h <= kHarmonics; ++h) {
            const double w = 2.0 * kPi * (static_cast<double>(h) * freqHz) * t;
            X(i, col++) = std::sin(w);
            X(i, col++) = std::cos(w);
        }
    }
    return X;
}
}

void SSVEPDetector::Reset() {
    voteFilled = 0;
    voteWrite = 0;
    snap = {};
    snap.refreshHz = refreshHz;
    snap.windowSec = static_cast<float>(kSSVEPWindowSec);
}

void SSVEPDetector::SetRefreshHz(int hz) {
    const int next = SSVEPRefreshOrDefault(hz);
    if (next == refreshHz) return;
    refreshHz = next;
    voteFilled = 0;
    voteWrite = 0;
    snap.refreshHz = refreshHz;
    snap.ready = false;
    snap.windowWinner = -1;
    snap.votedWinner = -1;
    snap.votesCast = 0;
    snap.votesForWinner = 0;
}

void SSVEPDetector::SetTargetFrequencies(const float hz[4]) {
    for (int i = 0; i < 4; ++i) {
        snap.targetHz[i] = hz[i];
    }
}

void SSVEPDetector::NoteProgress(float secondsCollected, int fs) {
    snap.secondsCollected = std::max(0.0f, secondsCollected);
    if (fs > 0) snap.samplingRate = fs;
    snap.refreshHz = refreshHz;
    snap.windowSec = static_cast<float>(kSSVEPWindowSec);
}

void SSVEPDetector::PushVote(int target, float score) {
    voteTarget[voteWrite] = target;
    voteScore[voteWrite] = score;
    voteWrite = (voteWrite + 1) % kSSVEPVoteLength;
    if (voteFilled < kSSVEPVoteLength) voteFilled++;
}

void SSVEPDetector::ResolveVote(int& winner, int& votesFor, int& cast) const {
    winner = -1;
    votesFor = 0;
    cast = voteFilled;
    if (voteFilled <= 0) return;

    int counts[4] = {};
    float scoreSum[4] = {};
    for (int i = 0; i < voteFilled; ++i) {
        const int slot = (voteWrite - voteFilled + i + kSSVEPVoteLength * 2) % kSSVEPVoteLength;
        const int target = voteTarget[slot];
        if (target < 0 || target > 3) continue;
        counts[target]++;
        scoreSum[target] += voteScore[slot];
    }
    winner = 0;
    for (int t = 1; t < 4; ++t) {
        if (counts[t] > counts[winner]
            || (counts[t] == counts[winner] && scoreSum[t] > scoreSum[winner])) {
            winner = t;
        }
    }
    votesFor = counts[winner];
}

void SSVEPDetector::Analyze(const float* const channels[4], const bool enabled[4], int nSamples, int fs) {
    snap.refreshHz = refreshHz;
    snap.samplingRate = fs;
    snap.windowSec = static_cast<float>(kSSVEPWindowSec);
    if (nSamples < 32 || fs < 30 || !channels || !enabled) return;

    int used[4];
    int nCh = 0;
    for (int ch = 0; ch < 4; ++ch) {
        if (enabled[ch] && channels[ch]) used[nCh++] = ch;
    }
    if (nCh <= 0) return;

    SSVEPFlicker flickers[kSSVEPTargetCount];
    ComputeSSVEPFlickers(refreshHz, flickers);
    for (int i = 0; i < 4; ++i) snap.targetHz[i] = flickers[i].realizedHz;

    const double lo = std::max(1.0, static_cast<double>(flickers[0].realizedHz) - 1.5);
    double hi = static_cast<double>(flickers[3].realizedHz) * 2.0 + 3.0;
    hi = std::min(46.0, hi);
    if (hi <= lo + 2.0) hi = lo + 2.0;

    std::vector<std::vector<double>> filtered(static_cast<size_t>(nCh));
    try {
        for (int c = 0; c < nCh; ++c) {
            filtered[static_cast<size_t>(c)].resize(static_cast<size_t>(nSamples));
            double* row = filtered[static_cast<size_t>(c)].data();
            const float* src = channels[used[c]];
            for (int i = 0; i < nSamples; ++i) row[i] = static_cast<double>(src[i]);
            DataFilter::perform_bandstop(row, nSamples, fs, 48.0, 52.0, 4, (int)FilterTypes::BUTTERWORTH, 0.0);
            DataFilter::perform_bandstop(row, nSamples, fs, 58.0, 62.0, 4, (int)FilterTypes::BUTTERWORTH, 0.0);
            DataFilter::perform_bandpass(row, nSamples, fs, lo, hi, 4, (int)FilterTypes::BUTTERWORTH, 0.0);
        }
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[SSVEP] filter: " << err.what() << std::endl;
        return;
    }

    Eigen::MatrixXd Y(nSamples, nCh);
    for (int c = 0; c < nCh; ++c) {
        const auto& row = filtered[static_cast<size_t>(c)];
        for (int i = 0; i < nSamples; ++i) Y(i, c) = row[static_cast<size_t>(i)];
    }

    int best = 0;
    float bestCorr = -1.0f;
    for (int t = 0; t < 4; ++t) {
        const Eigen::MatrixXd X = ReferenceMatrix(flickers[t].realizedHz, nSamples, fs);
        const float corr = static_cast<float>(CanonicalCorrelation(X, Y));
        snap.corr[t] = corr;
        if (corr > bestCorr) {
            bestCorr = corr;
            best = t;
        }
    }
    snap.windowWinner = best;
    PushVote(best, bestCorr);
    ResolveVote(snap.votedWinner, snap.votesForWinner, snap.votesCast);

    snap.spectrumCount = 0;
    std::vector<double> acc;
    std::vector<double> freqs;
    int nPsd = 0;
    try {
        for (int c = 0; c < nCh; ++c) {
            int psdLen = 0;
            auto psd = DataFilter::get_psd(
                filtered[static_cast<size_t>(c)].data(), nSamples, fs,
                (int)WindowOperations::HANNING, &psdLen);
            if (psdLen <= 0 || !psd.first || !psd.second) {
                delete[] psd.first;
                delete[] psd.second;
                continue;
            }
            if (acc.empty()) {
                acc.assign(static_cast<size_t>(psdLen), 0.0);
                freqs.assign(static_cast<size_t>(psdLen), 0.0);
                for (int i = 0; i < psdLen; ++i) freqs[static_cast<size_t>(i)] = psd.second[i];
            }
            const int n = std::min(psdLen, static_cast<int>(acc.size()));
            for (int i = 0; i < n; ++i) acc[static_cast<size_t>(i)] += psd.first[i];
            ++nPsd;
            delete[] psd.first;
            delete[] psd.second;
        }
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[SSVEP] psd: " << err.what() << std::endl;
    }

    if (nPsd > 0) {
        int count = 0;
        for (int i = 0; i < static_cast<int>(acc.size()) && count < kSSVEPSpectrumCapacity; ++i) {
            const float f = static_cast<float>(freqs[static_cast<size_t>(i)]);
            if (f < 4.0f || f > 46.0f) continue;
            snap.freq[static_cast<size_t>(count)] = f;
            snap.power[static_cast<size_t>(count)] = static_cast<float>(acc[static_cast<size_t>(i)] / nPsd);
            ++count;
        }
        snap.spectrumCount = count;
    }

    snap.secondsCollected = static_cast<float>(nSamples) / static_cast<float>(fs);
    snap.ready = snap.spectrumCount > 2 && snap.windowWinner >= 0;
}
