#pragma once

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline constexpr float kSSVEPTargetHz[4] = {
    7.0f,
    12.0f,
    15.0f,
    17.0f
};

// Plot bandpass around the board frequencies: lowest minus 2 Hz,
// and the second harmonic of the highest plus 2 Hz.
constexpr double SSVEPPlotBandLowHz(const float hz[4])
{
    float minHz = hz[0];
    for (int i = 1; i < 4; ++i) {
        if (hz[i] < minHz) minHz = hz[i];
    }
    const double lo = static_cast<double>(minHz) - 2.0;
    return (lo < 0.5) ? 0.5 : lo;
}

constexpr double SSVEPPlotBandHighHz(const float hz[4])
{
    float maxHz = hz[0];
    for (int i = 1; i < 4; ++i) {
        if (hz[i] > maxHz) maxHz = hz[i];
    }
    return static_cast<double>(maxHz) * 2.0 + 2.0;
}

inline int SSVEPRefreshOrDefault(int refreshHz)
{
    return (refreshHz >= 30) ? refreshHz : 60;
}

inline float SSVEPSineAlpha(double timeSec, float targetHz)
{
    double sineValue =
        std::sin(2.0 * M_PI *
                 static_cast<double>(targetHz) *
                 timeSec);

    return static_cast<float>((sineValue + 1.0) * 0.5);
}

inline bool SSVEPSquareOn(double timeSec, float targetHz)
{
    const double phase =
        std::fmod(timeSec * static_cast<double>(targetHz), 1.0);

    return phase < 0.5;
}
