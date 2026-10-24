#pragma once

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline constexpr float kSSVEPTargetHz[4] = {
    13.0f,
    15.0f,
    17.0f,
    19.0f
};

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
