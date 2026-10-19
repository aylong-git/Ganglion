#pragma once

#include <algorithm>
#include <cmath>

// Nominal SSVEP targets. On a 60 Hz monitor these are exact integer frame
// divisions (6, 5, 4, 3 frames/cycle). Higher refresh rates use the nearest
// integer period; realizedHz is the rate the display actually produces.
inline constexpr float kSSVEPNominalHz[4] = { 10.0f, 12.0f, 15.0f, 20.0f };
inline constexpr int kSSVEPTargetCount = 4;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct SSVEPFlicker {
    float nominalHz = 0.0f;
    float realizedHz = 0.0f;
    int periodFrames = 2;
};

inline int SSVEPRefreshOrDefault(int refreshHz) {
    return (refreshHz >= 30) ? refreshHz : 60;
}

inline void ComputeSSVEPFlickers(int refreshHz, SSVEPFlicker out[kSSVEPTargetCount], const float targetHz[kSSVEPTargetCount] = kSSVEPNominalHz) {
    const int refresh = SSVEPRefreshOrDefault(refreshHz);
    for (int i = 0; i < kSSVEPTargetCount; ++i) {
        int period = static_cast<int>(std::lround(
            static_cast<double>(refresh) / static_cast<double>(targetHz[i])));
        period = std::max(2, period);
        out[i].nominalHz = targetHz[i];
        out[i].periodFrames = period;
        out[i].realizedHz = static_cast<float>(refresh) / static_cast<float>(period);
    }
}

inline float SSVEPSineAlpha(unsigned long long frame, int refreshHz, float targetHz) {
    // Convert current frame to actual time in seconds
    double timeSec = static_cast<double>(frame) / static_cast<double>(refreshHz);
    
    // Calculate the sine wave: sin(2 * PI * f * t)
    // This oscillates between -1.0 and 1.0
    double sineValue = std::sin(2.0 * M_PI * static_cast<double>(targetHz) * timeSec);
    
    // Normalize from [-1.0, 1.0] to [0.0, 1.0] for opacity/color tinting
    return static_cast<float>((sineValue + 1.0) * 0.5);
}

inline bool SSVEPSquareOn(unsigned long long frame, int periodFrames) {
    const int period = std::max(2, periodFrames);
    return (frame % static_cast<unsigned long long>(period))
        < static_cast<unsigned long long>(period / 2);
}
