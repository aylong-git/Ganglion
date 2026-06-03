#pragma once
#include <string>
#include <imgui.h>

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

    std::string GetKeyName(Target target) const {
        ImGuiKey key = (target == Target::Focus) ? m_focusKey : m_relaxKey;
        if (key == ImGuiKey_None) return "None";
        return ImGui::GetKeyName(key);
    }

    void Update();

    void ReleaseAllKey() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(m_relaxKey, false);
        io.AddKeyEvent(m_focusKey, false);
    }

    void SimulateHold(Target target) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent((target == Target::Focus) ? m_focusKey : m_relaxKey, true);
        io.AddMouseButtonEvent((target == Target::Focus) ? m_relaxKey : m_focusKey, false);
    }

    void SimulatePress(Target target) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent((target == Target::Focus) ? m_focusKey : m_relaxKey, true);
        io.AddKeyEvent((target == Target::Focus) ? m_focusKey : m_relaxKey, false);
    }

private:
    InputManager() = default;

    Target m_bindingTarget = Target::None;
    bool m_justActivated = false;
    ImGuiKey m_focusKey = ImGuiKey_UpArrow;
    ImGuiKey m_relaxKey = ImGuiKey_DownArrow;
};