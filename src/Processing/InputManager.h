#pragma once
#include <string>
#include <imgui.h>

// --- OS-Specific Headers ---
#if defined(_WIN32)
#include <windows.h>

#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#endif

class InputManager {
public:
    enum class Target { None = 0, Relax, Focus };

    static InputManager& GetInstance() {
        static InputManager instance;
        return instance;
    }

    void StartBinding(Target target) {
        m_bindingTarget = target;
        m_justActivated = true;
    }

    bool IsBinding() const { return m_bindingTarget != Target::None; }
    Target GetBindingTarget() const { return m_bindingTarget; }

    ImGuiKey GetKey(Target target) const {
        ImGuiKey key = (target == Target::Focus) ? m_focusKey : m_relaxKey;
        return key;
    }

    std::string GetKeyName(Target target) const {
        ImGuiKey key = (target == Target::Focus) ? m_focusKey : m_relaxKey;
        if (key == ImGuiKey_None) return "None";
        return ImGui::GetKeyName(key);
    }

    void Update();

private:
    InputManager() = default;

    Target m_bindingTarget = Target::None;
    bool m_justActivated = false;
    ImGuiKey m_focusKey = ImGuiKey_None;
    ImGuiKey m_relaxKey = ImGuiKey_None;
};


class OSInputSimulator {
public:
    static void SendHardwareKey(ImGuiKey key, bool Press);

    static void SimulateTap(InputManager::Target target) {
        ImGuiKey key = InputManager::GetInstance().GetKey(target);
        SendHardwareKey(key, true);
        SendHardwareKey(key, false);
    }

    static void SimulateHold(InputManager::Target target) {
        ImGuiKey key_focus = InputManager::GetInstance().GetKey(InputManager::Target::Focus);
        ImGuiKey key_relax = InputManager::GetInstance().GetKey(InputManager::Target::Relax);
        if (target == InputManager::Target::Focus) {
            SendHardwareKey(key_focus, true);
            SendHardwareKey(key_relax, false);
        }
        else {
            SendHardwareKey(key_relax, true);
            SendHardwareKey(key_focus, false);
        }
    }

    static void ReleaseAllKey() {
        ImGuiKey key_focus = InputManager::GetInstance().GetKey(InputManager::Target::Focus);
        ImGuiKey key_relax = InputManager::GetInstance().GetKey(InputManager::Target::Relax);
        SendHardwareKey(key_focus, false);
        SendHardwareKey(key_relax, false);
    }

private:
    // Internal translation functions
#if defined(_WIN32)
    static WORD TranslateToWindowsVK(ImGuiKey key);
#elif defined(__APPLE__)
    static CGKeyCode TranslateToMacVK(ImGuiKey key);
#endif
};