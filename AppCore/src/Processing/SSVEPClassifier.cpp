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

// Peak SNR on a Hann periodogram. Noise bins start `guard` bins off the line so the
// Hann main lobe is not treated as noise. A shoulder below the local baseline scores 0.
double PeakSnr(const double* power, const double* freqs, int nFreq, double targetHz, int guard, int nBins) {
    int idx = 0;
    double minDiff = 1e300;
    for (int i = 0; i < nFreq; ++i) {
        const double diff = std::abs(freqs[i] - targetHz);
        if (diff < minDiff) {
            minDiff = diff;
            idx = i;
        }
    }

    const int far = guard + nBins - 1;
    const int leftLo = idx - far;
    const int leftHi = idx - guard;
    const int rightLo = idx + guard;
    const int rightHi = idx + far;
    if (leftLo < 0 || rightHi >= nFreq) return 0.0;

    double sumL = 0.0;
    double sumR = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    int count = 0;
    for (int i = leftLo; i <= leftHi; ++i) {
        const double x = freqs[i];
        const double y = power[i];
        sumL += y;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++count;
    }
    for (int i = rightLo; i <= rightHi; ++i) {
        const double x = freqs[i];
        const double y = power[i];
        sumR += y;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++count;
    }
    if (count < 2) return 0.0;

    const double denom = static_cast<double>(count) * sumXX - sumX * sumX;
    if (std::abs(denom) < 1e-18) return 0.0;
    const double slope = (static_cast<double>(count) * sumXY - sumX * sumY) / denom;
    const double intercept = (sumY - slope * sumX) / static_cast<double>(count);
    const double signal = power[idx];
    const double excess = signal - (slope * freqs[idx] + intercept);
    if (excess <= 0.0) return 0.0;

    const int sideCount = leftHi - leftLo + 1;
    const double noise = std::max(sumL / sideCount, sumR / sideCount);
    if (noise <= 0.0) return 0.0;
    return signal / noise;
}

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

constexpr float kCcaPenalty = 0.20f;
constexpr float kFbccaA = 1.25f;
constexpr float kFbccaB = 0.25f;

void BandEdges(const float targetHz[4], int fs, double& low, double& high) {
    float minHz = targetHz[0];
    float maxHz = targetHz[0];
    for (int i = 1; i < 4; ++i) {
        minHz = std::min(minHz, targetHz[i]);
        maxHz = std::max(maxHz, targetHz[i]);
    }
    low = std::floor(static_cast<double>(minHz) - 2.0);
    high = std::floor(static_cast<double>(maxHz) * 2.0 + 2.0);
    const double nyquist = 0.5 * static_cast<double>(fs) - 1.0;
    if (low < 0.5) low = 0.5;
    if (high > nyquist) high = nyquist;
    if (high < low + 6.0) high = std::min(nyquist, low + 6.0);
}

struct FbccaBand {
    double low = 0.0;
    double high = 0.0;
    double weight = 0.0;
};

int BuildFbccaBands(const float targetHz[4], int fs, FbccaBand* out, int cap) {
    double bpLow = 0.0;
    double bpHigh = 0.0;
    BandEdges(targetHz, fs, bpLow, bpHigh);
    float base = targetHz[0];
    for (int i = 1; i < 4; ++i) base = std::min(base, targetHz[i]);
    int n = 0;
    for (int harmonic = 1; harmonic <= 8 && n < cap; ++harmonic) {
        const double low = std::max(bpLow, static_cast<double>(harmonic) * base - 2.0);
        if (bpHigh - low < 6.0) break;
        out[n].low = low;
        out[n].high = bpHigh;
        out[n].weight = std::pow(static_cast<double>(harmonic), -static_cast<double>(kFbccaA)) + kFbccaB;
        ++n;
    }
    if (n == 0 && cap > 0) {
        out[0].low = bpLow;
        out[0].high = bpHigh;
        out[0].weight = 1.0;
        n = 1;
    }
    return n;
}

double PenalizedCca(const Eigen::MatrixXd& eeg, const Eigen::MatrixXd& ref, const Eigen::VectorXd& penalty) {
    const int n = static_cast<int>(eeg.rows());
    if (n < 8 || eeg.cols() < 1 || ref.cols() < 1 || penalty.size() != eeg.cols()) return 0.0;

    Eigen::MatrixXd X = eeg.rowwise() - eeg.colwise().mean();
    Eigen::MatrixXd Y = ref.rowwise() - ref.colwise().mean();
    auto scaleCols = [](Eigen::MatrixXd& m) {
        const double denom = static_cast<double>(m.rows());
        for (int c = 0; c < m.cols(); ++c) {
            const double sd = std::sqrt(m.col(c).squaredNorm() / denom);
            m.col(c) /= (sd < 1e-12) ? 1.0 : sd;
        }
    };
    scaleCols(X);
    scaleCols(Y);

    Eigen::MatrixXd Cxx = (X.transpose() * X) / static_cast<double>(n);
    Eigen::MatrixXd Cyy = (Y.transpose() * Y) / static_cast<double>(n);
    const Eigen::MatrixXd Cxy = (X.transpose() * Y) / static_cast<double>(n);
    for (int c = 0; c < Cxx.cols(); ++c) Cxx(c, c) += std::max(0.0, penalty(c));
    Cyy.diagonal().array() += 1e-6;

    Eigen::LDLT<Eigen::MatrixXd> ldltX(Cxx);
    Eigen::LDLT<Eigen::MatrixXd> ldltY(Cyy);
    if (ldltX.info() != Eigen::Success || ldltY.info() != Eigen::Success) return 0.0;

    const Eigen::MatrixXd M = ldltX.solve(Cxy) * ldltY.solve(Cxy.transpose());
    Eigen::EigenSolver<Eigen::MatrixXd> eigen(M, false);
    if (eigen.info() != Eigen::Success) return 0.0;
    double best = 0.0;
    for (int i = 0; i < eigen.eigenvalues().size(); ++i) {
        best = std::max(best, eigen.eigenvalues()(i).real());
    }
    best = std::min(1.0, std::max(0.0, best));
    return std::sqrt(best);
}

int CopyEven(const float* src, int n, std::vector<double>& out) {
    if (!src || n < 32) return 0;
    if (n % 2) --n;
    out.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out[static_cast<size_t>(i)] = static_cast<double>(src[i]);
    return n;
}

bool PowerSpectrum(std::vector<double>& row, int fs, std::vector<double>& power, std::vector<double>& freqs) {
    const int n = static_cast<int>(row.size());
    if (n < 32 || (n % 2) != 0) return false;
    int psdLen = 0;
    auto psd = DataFilter::get_psd(row.data(), n, fs, (int)WindowOperations::HANNING, &psdLen);
    if (psdLen <= 1 || !psd.first || !psd.second) {
        delete[] psd.first;
        delete[] psd.second;
        return false;
    }
    power.resize(static_cast<size_t>(psdLen));
    freqs.resize(static_cast<size_t>(psdLen));
    for (int i = 0; i < psdLen; ++i) {
        const double ampl = psd.first[i];
        power[static_cast<size_t>(i)] = ampl * ampl;
        freqs[static_cast<size_t>(i)] = psd.second[i];
    }
    delete[] psd.first;
    delete[] psd.second;
    return true;
}
}

bool MeasureHarmonicSnr(const float* const channels[4], int nSamples, int fs,
    const float targetHz[4], float outSnr[4], const float channelWeight[4]) {
    for (int t = 0; t < 4; ++t) outSnr[t] = 0.0f;
    if (!channels || !targetHz || nSamples < 32 || fs < 30) return false;
    const float* weight = channelWeight ? channelWeight : kChannelWeightPrior;

    double bpLow = 0.0;
    double bpHigh = 0.0;
    BandEdges(targetHz, fs, bpLow, bpHigh);

    bool any = false;
    for (int ch = 0; ch < 4; ++ch) {
        if (!channels[ch] || !(weight[ch] > 0.0f)) continue;
        std::vector<double> row;
        const int n = CopyEven(channels[ch], nSamples, row);
        if (n < 32) continue;
        try {
            DataFilter::detrend(row.data(), n, (int)DetrendOperations::CONSTANT);
            DataFilter::perform_bandstop(row.data(), n, fs, 48.0, 52.0, 4,
                (int)FilterTypes::BUTTERWORTH, 0.0);
            DataFilter::perform_bandpass(row.data(), n, fs, bpLow, bpHigh, 6,
                (int)FilterTypes::BUTTERWORTH, 0.0);
            std::vector<double> power;
            std::vector<double> freqs;
            if (!PowerSpectrum(row, fs, power, freqs)) continue;
            const int nFreq = static_cast<int>(power.size());
            for (int t = 0; t < 4; ++t) {
                const double fundamental = PeakSnr(power.data(), freqs.data(), nFreq, targetHz[t], 2, 3);
                const double harmonic = PeakSnr(power.data(), freqs.data(), nFreq, targetHz[t] * 2.0, 2, 3);
                outSnr[t] += weight[ch] * static_cast<float>(fundamental + 0.5 * harmonic);
            }
            any = true;
        }
        catch (const BrainFlowException&) {
        }
    }
    return any;
}

bool ChannelWeightsFromNull(const float* const channels[4], int nSamples, int fs,
    const float targetHz[4], float outWeight[4]) {
    for (int ch = 0; ch < 4; ++ch) outWeight[ch] = kChannelWeightPrior[ch];
    if (!channels || !targetHz || nSamples < 32 || fs < 30) return false;

    double bpLow = 0.0;
    double bpHigh = 0.0;
    BandEdges(targetHz, fs, bpLow, bpHigh);

    double dirt[4] = {};
    bool usable[4] = {};
    int nUsed = 0;
    for (int ch = 0; ch < 4; ++ch) {
        if (!channels[ch]) continue;
        std::vector<double> row;
        const int n = CopyEven(channels[ch], nSamples, row);
        if (n < 32) continue;

        double mean = 0.0;
        int rail = 0;
        for (int i = 0; i < n; ++i) {
            mean += row[static_cast<size_t>(i)];
            if (std::fabs(row[static_cast<size_t>(i)] + 15686.3) < 5.0) ++rail;
        }
        mean /= static_cast<double>(n);
        double var = 0.0;
        for (int i = 0; i < n; ++i) {
            const double d = row[static_cast<size_t>(i)] - mean;
            var += d * d;
        }
        var /= static_cast<double>(n);
        if (var < 0.05 * 0.05 || rail > n / 2) continue;

        try {
            DataFilter::detrend(row.data(), n, (int)DetrendOperations::CONSTANT);
            DataFilter::perform_bandstop(row.data(), n, fs, 48.0, 52.0, 4,
                (int)FilterTypes::BUTTERWORTH, 0.0);
            DataFilter::perform_bandpass(row.data(), n, fs, bpLow, bpHigh, 6,
                (int)FilterTypes::BUTTERWORTH, 0.0);
            std::vector<double> power;
            std::vector<double> freqs;
            if (!PowerSpectrum(row, fs, power, freqs)) continue;
            std::vector<double> band;
            band.reserve(power.size());
            for (size_t i = 0; i < power.size(); ++i) {
                if (freqs[i] < bpLow || freqs[i] > bpHigh) continue;
                const double p = power[i];
                if (std::isfinite(p) && p >= 0.0) band.push_back(p);
            }
            if (band.size() < 4) continue;
            std::sort(band.begin(), band.end());
            const double med = band[band.size() / 2];
            if (!(med > 0.0) || !std::isfinite(med)) continue;
            double mx = band.back();
            double noise = med;
            if (mx > med * 8.0) noise = mx / 8.0;
            dirt[ch] = noise;
            usable[ch] = true;
            ++nUsed;
        }
        catch (const BrainFlowException&) {
        }
    }
    if (nUsed == 0) return false;

    double raw[4] = {};
    double sum = 0.0;
    for (int ch = 0; ch < 4; ++ch) {
        if (!usable[ch]) continue;
        raw[ch] = static_cast<double>(kChannelWeightPrior[ch]) / dirt[ch];
        sum += raw[ch];
    }
    if (!(sum > 0.0)) return false;

    bool frozen[4] = {};
    double w[4] = {};
    for (int ch = 0; ch < 4; ++ch) {
        if (usable[ch]) w[ch] = raw[ch] / sum;
    }
    for (int iter = 0; iter < 4; ++iter) {
        double freeSum = 0.0;
        double held = 0.0;
        int freeN = 0;
        for (int ch = 0; ch < 4; ++ch) {
            if (!usable[ch]) continue;
            if (frozen[ch]) held += w[ch];
            else {
                freeSum += w[ch];
                ++freeN;
            }
        }
        if (freeN == 0) break;
        const double room = std::max(0.0, 1.0 - held);
        bool hit = false;
        for (int ch = 0; ch < 4; ++ch) {
            if (!usable[ch] || frozen[ch]) continue;
            w[ch] = (freeSum > 0.0) ? (w[ch] / freeSum * room) : 0.0;
            if (w[ch] > 0.60) {
                w[ch] = 0.60;
                frozen[ch] = true;
                hit = true;
            }
            else if (w[ch] < 0.05) {
                w[ch] = 0.05;
                frozen[ch] = true;
                hit = true;
            }
        }
        if (!hit) break;
    }
    double norm = 0.0;
    for (int ch = 0; ch < 4; ++ch) norm += w[ch];
    if (!(norm > 0.0)) return false;
    for (int ch = 0; ch < 4; ++ch) outWeight[ch] = static_cast<float>(w[ch] / norm);
    return true;
}

float HannPeakPower(const float* const channels[4], int nSamples, int fs, float targetHz) {
    if (!channels || nSamples < 32 || fs < 30) return 0.0f;
    float peak = 0.0f;
    bool any = false;
    for (int ch = 0; ch < 4; ++ch) {
        if (!channels[ch]) continue;
        std::vector<double> row;
        const int n = CopyEven(channels[ch], nSamples, row);
        if (n < 32) continue;
        try {
            ApplyDisplaySpectrumFilter(row.data(), n, fs);
            std::vector<double> power;
            std::vector<double> freqs;
            if (!PowerSpectrum(row, fs, power, freqs)) continue;
            int idx = 0;
            double best = 1e300;
            for (int i = 0; i < static_cast<int>(freqs.size()); ++i) {
                const double diff = std::abs(freqs[static_cast<size_t>(i)] - static_cast<double>(targetHz));
                if (diff < best) {
                    best = diff;
                    idx = i;
                }
            }
            peak = std::max(peak, static_cast<float>(power[static_cast<size_t>(idx)]));
            any = true;
        }
        catch (const BrainFlowException&) {
        }
    }
    return any ? peak : 0.0f;
}

void ApplyDisplaySpectrumFilter(double* data, int n, int fs, double lowHz, double highHz) {
    if (!data || n < 32 || fs < 30) return;
    const double nyquist = 0.5 * static_cast<double>(fs) - 1.0;
    double high = highHz;
    if (high > nyquist) high = nyquist;
    const double low = lowHz;
    if (!(high > low + 1.0)) return;
    DataFilter::detrend(data, n, (int)DetrendOperations::CONSTANT);
    DataFilter::perform_bandstop(data, n, fs, 48.0, 52.0, 4, (int)FilterTypes::BUTTERWORTH, 0.0);
    DataFilter::perform_bandpass(data, n, fs, low, high, 4, (int)FilterTypes::BUTTERWORTH, 0.0);
}

void SSVEPDetector::SetChannelWeights(const float weight[4]) {
    if (!weight) return;
    for (int ch = 0; ch < 4; ++ch) {
        channelWeight[ch] = weight[ch] > 0.0f ? weight[ch] : 0.0f;
    }
}

void SSVEPDetector::SetWindowConfig(float windowSeconds, float hopSeconds) {
    if (windowSeconds == windowSec && hopSeconds == hopSec) return;
    windowSec = windowSeconds;
    hopSec = hopSeconds;
    snap.windowSec = windowSec;
    snap.hopSec = hopSec;
    Reset(); // Reset internal buffers to prevent stride index mismatches
}

void SSVEPDetector::Reset() {
    voteFilled = 0;
    voteWrite = 0;
    snap = {};
    snap.refreshHz = refreshHz;
    snap.windowSec = windowSec;
    snap.hopSec = hopSec;
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
    snap.windowSec = windowSec;
    snap.hopSec = hopSec;
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
    snap.windowSec = windowSec;
    snap.hopSec = hopSec;
    if (nSamples < 32 || fs < 30 || !channels || !enabled) return;

    int used[4];
    int nCh = 0;
    for (int ch = 0; ch < 4; ++ch) {
        if (enabled[ch] && channels[ch] && channelWeight[ch] > 0.0f) used[nCh++] = ch;
    }
    if (nCh <= 0) return;

    const float* snrChannels[4] = {};
    for (int c = 0; c < nCh; ++c) snrChannels[used[c]] = channels[used[c]];
    float harmonic[4] = {};
    if (MeasureHarmonicSnr(snrChannels, nSamples, fs, snap.targetHz, harmonic, channelWeight)) {
        for (int t = 0; t < 4; ++t) snap.harmonicSnr[t] = harmonic[t];
    }

    FbccaBand bands[8];
    const int nBands = BuildFbccaBands(snap.targetHz, fs, bands, 8);

    std::vector<std::vector<double>> wide(static_cast<size_t>(nCh));
    double bpLow = 0.0;
    double bpHigh = 0.0;
    BandEdges(snap.targetHz, fs, bpLow, bpHigh);
    try {
        for (int c = 0; c < nCh; ++c) {
            wide[static_cast<size_t>(c)].resize(static_cast<size_t>(nSamples));
            double* row = wide[static_cast<size_t>(c)].data();
            const float* src = channels[used[c]];
            for (int i = 0; i < nSamples; ++i) row[i] = static_cast<double>(src[i]);
            DataFilter::detrend(row, nSamples, (int)DetrendOperations::CONSTANT);
            DataFilter::perform_bandstop(row, nSamples, fs, 48.0, 52.0, 4, (int)FilterTypes::BUTTERWORTH, 0.0);
            DataFilter::perform_bandpass(row, nSamples, fs, bpLow, bpHigh, 6, (int)FilterTypes::BUTTERWORTH, 0.0);
        }
    }
    catch (const BrainFlowException& err) {
        std::cerr << "[SSVEP] filter: " << err.what() << std::endl;
        return;
    }

    float weightMax = 0.0f;
    for (int c = 0; c < nCh; ++c) weightMax = std::max(weightMax, channelWeight[used[c]]);
    Eigen::VectorXd penalty(nCh);
    for (int c = 0; c < nCh; ++c) {
        const float weight = std::max(channelWeight[used[c]], 1.0e-4f);
        penalty(c) = static_cast<double>(kCcaPenalty) * (static_cast<double>(weightMax) / weight - 1.0);
    }

    float scores[4] = {};
    bool scored = false;
    for (int b = 0; b < nBands; ++b) {
        Eigen::MatrixXd Y(nSamples, nCh);
        try {
            for (int c = 0; c < nCh; ++c) {
                std::vector<double> band = wide[static_cast<size_t>(c)];
                DataFilter::perform_bandpass(band.data(), nSamples, fs, bands[b].low, bands[b].high, 4,
                    (int)FilterTypes::BUTTERWORTH, 0.0);
                for (int i = 0; i < nSamples; ++i) Y(i, c) = band[static_cast<size_t>(i)];
            }
        }
        catch (const BrainFlowException& err) {
            std::cerr << "[SSVEP] sub-band: " << err.what() << std::endl;
            continue;
        }
        for (int t = 0; t < 4; ++t) {
            const Eigen::MatrixXd ref = ReferenceMatrix(snap.targetHz[t], nSamples, fs);
            const double rho = PenalizedCca(Y, ref, penalty);
            scores[t] += static_cast<float>(bands[b].weight * rho * rho);
        }
        scored = true;
    }
    if (!scored) return;

    int best = 0;
    for (int t = 0; t < 4; ++t) {
        snap.corr[t] = scores[t];
        if (scores[t] > scores[best]) best = t;
    }
    snap.windowWinner = best;
    snap.votedWinner = best;
    snap.votesForWinner = 1;
    snap.votesCast = 1;
    snap.spectrumCount = 0;
    snap.secondsCollected = static_cast<float>(nSamples) / static_cast<float>(fs);
    snap.ready = true;
    snap.epoch++;
}

