#pragma once
#include <atomic>

unsigned int LoadComponentTexture(const char* filepath);

namespace Config {
    inline std::atomic<float> focusThreshold{ 0.7f };
    inline std::atomic<int> inputModeHold{ 0 };
}