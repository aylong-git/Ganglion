// 1. Silence the Visual Studio deprecation warnings
#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
#define _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS

// 2. Prevent Windows from breaking C++ math functions
#define NOMINMAX 

// 3. The "Kitchen Sink" Standard C++ Libraries that BrainFlow secretly needs
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <tuple>
#include <map>

// 4. Include Dear ImGui (vcpkg uses angle brackets for external libs)
#include <imgui.h>

// 5. Include BrainFlow
#include "board_shim.h"
#include "data_filter.h"

int main() {
    // Test standard C++
    std::cout << "Hello from C++!" << std::endl;

    // Test ImGui linking
    ImGui::CreateContext();
    std::cout << "Dear ImGui context created successfully." << std::endl;

    // Test BrainFlow linking
    std::cout << "BrainFlow Ganglion Board ID: " << (int)BoardIds::GANGLION_BOARD << std::endl;

    // Clean up
    ImGui::DestroyContext();

    std::cout << "\nAll libraries linked successfully! Press Enter to close." << std::endl;
    std::cin.get();

    return 0;
}