#pragma once
#include <string>
#include <imgui.h>

#if defined(_WIN32)
#include <windows.h>

#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#endif

class InputManager {
public:
    static constexpr int kBoardCount = 4;

    static InputManager& GetInstance() {
        static InputManager instance;
        return instance;
    }

    void ResetAllKeys() {
        for (int i = 0; i < kBoardCount; ++i) {
            m_keys[i] = ImGuiKey_None;
        }
        m_bindingBoard = -1;
    }

    void StartBinding(int boardIndex) {
        if (boardIndex < 0 || boardIndex >= kBoardCount) return;
        m_bindingBoard = boardIndex;
        m_justActivated = true;
    }

    bool IsBinding() const { return m_bindingBoard >= 0; }
    int GetBindingBoard() const { return m_bindingBoard; }

    ImGuiKey GetKey(int boardIndex) const {
        if (boardIndex < 0 || boardIndex >= kBoardCount) return ImGuiKey_None;
        return m_keys[boardIndex];
    }

    std::string GetKeyName(int boardIndex) const {
        ImGuiKey key = GetKey(boardIndex);
        if (key == ImGuiKey_None) return "None";
        return ImGui::GetKeyName(key);
    }

    void Update();

private:
    InputManager() = default;

    int m_bindingBoard = -1;
    bool m_justActivated = false;
    ImGuiKey m_keys[kBoardCount] = {
        ImGuiKey_None, ImGuiKey_None, ImGuiKey_None, ImGuiKey_None
    };
};


class OSInputSimulator {
public:
    static void SendHardwareKey(ImGuiKey key, bool Press);

    static void SimulateTap(ImGuiKey key) {
        SendHardwareKey(key, true);
        SendHardwareKey(key, false);
    }

    static void ReleaseKey(ImGuiKey key) {
        SendHardwareKey(key, false);
    }

    static void ReleaseAllKey() {
        for (int i = 0; i < InputManager::kBoardCount; ++i) {
            ImGuiKey key = InputManager::GetInstance().GetKey(i);
            if (key != ImGuiKey_None) {
                SendHardwareKey(key, false);
            }
        }
    }

private:
    // Internal translation functions
#if defined(_WIN32)
    static WORD TranslateToWindowsVK(ImGuiKey key);
#elif defined(__APPLE__)
    static CGKeyCode TranslateToMacVK(ImGuiKey key);
#endif
};
