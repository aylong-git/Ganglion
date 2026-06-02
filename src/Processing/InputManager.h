#pragma once
#include <string>
#include <imgui.h>

class InputManager {
public:
    enum class Target { None = 0, Relax, Focus };

    // Singleton access or standard instance reference
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

    std::string GetKeyName(Target target) const {
        ImGuiKey key = (target == Target::Focus) ? m_focusKey : m_relaxKey;
        if (key == ImGuiKey_None) return "None";
        return ImGui::GetKeyName(key);
    }

    // Call this once per frame before rendering the UI
    void Update();

private:
    InputManager() = default;

    Target m_bindingTarget = Target::None;
    bool m_justActivated = false;
    ImGuiKey m_focusKey = ImGuiKey_None;
    ImGuiKey m_relaxKey = ImGuiKey_None;
};