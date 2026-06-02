#include "InputManager.h"

void InputManager::Update() {
    if (m_bindingTarget == Target::None) return;

    if (m_justActivated) {
        m_justActivated = false;
        return;
    }

    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
        if (ImGui::IsKeyPressed((ImGuiKey)key)) {
            if (m_bindingTarget == Target::Focus) {
                m_focusKey = (ImGuiKey)key;
            }
            else if (m_bindingTarget == Target::Relax) {
                m_relaxKey = (ImGuiKey)key;
            }
            m_bindingTarget = Target::None;
            break;
        }
    }
}