#include "InputManager.h"

#if defined(_WIN32)

WORD OSInputSimulator::TranslateToWindowsVK(ImGuiKey key) {
    switch (key) {
        // --- Arrows ---
    case ImGuiKey_UpArrow: return VK_UP;       case ImGuiKey_DownArrow: return VK_DOWN;
    case ImGuiKey_LeftArrow: return VK_LEFT;   case ImGuiKey_RightArrow: return VK_RIGHT;

        // --- Numbers ---
    case ImGuiKey_0: return '0'; case ImGuiKey_1: return '1'; case ImGuiKey_2: return '2';
    case ImGuiKey_3: return '3'; case ImGuiKey_4: return '4'; case ImGuiKey_5: return '5';
    case ImGuiKey_6: return '6'; case ImGuiKey_7: return '7'; case ImGuiKey_8: return '8';
    case ImGuiKey_9: return '9';

        // --- Numpad ---
    case ImGuiKey_Keypad0: return VK_NUMPAD0; case ImGuiKey_Keypad1: return VK_NUMPAD1;
    case ImGuiKey_Keypad2: return VK_NUMPAD2; case ImGuiKey_Keypad3: return VK_NUMPAD3;
    case ImGuiKey_Keypad4: return VK_NUMPAD4; case ImGuiKey_Keypad5: return VK_NUMPAD5;
    case ImGuiKey_Keypad6: return VK_NUMPAD6; case ImGuiKey_Keypad7: return VK_NUMPAD7;
    case ImGuiKey_Keypad8: return VK_NUMPAD8; case ImGuiKey_Keypad9: return VK_NUMPAD9;

        // --- Alphabet ---
    case ImGuiKey_A: return 'A'; case ImGuiKey_B: return 'B'; case ImGuiKey_C: return 'C';
    case ImGuiKey_D: return 'D'; case ImGuiKey_E: return 'E'; case ImGuiKey_F: return 'F';
    case ImGuiKey_G: return 'G'; case ImGuiKey_H: return 'H'; case ImGuiKey_I: return 'I';
    case ImGuiKey_J: return 'J'; case ImGuiKey_K: return 'K'; case ImGuiKey_L: return 'L';
    case ImGuiKey_M: return 'M'; case ImGuiKey_N: return 'N'; case ImGuiKey_O: return 'O';
    case ImGuiKey_P: return 'P'; case ImGuiKey_Q: return 'Q'; case ImGuiKey_R: return 'R';
    case ImGuiKey_S: return 'S'; case ImGuiKey_T: return 'T'; case ImGuiKey_U: return 'U';
    case ImGuiKey_V: return 'V'; case ImGuiKey_W: return 'W'; case ImGuiKey_X: return 'X';
    case ImGuiKey_Y: return 'Y'; case ImGuiKey_Z: return 'Z';

    default: return 0; // Unmapped/Invalid
    }
}

void OSInputSimulator::SendHardwareKey(ImGuiKey key, bool isDown) {
    INPUT input = { 0 };

    // --- 1. HANDLE MOUSE INPUTS ---
    if (key == ImGuiKey_MouseLeft || key == ImGuiKey_MouseRight || key == ImGuiKey_MouseMiddle) {
        input.type = INPUT_MOUSE;

        if (key == ImGuiKey_MouseLeft) {
            input.mi.dwFlags = isDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        }
        else if (key == ImGuiKey_MouseRight) {
            input.mi.dwFlags = isDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        }
        else if (key == ImGuiKey_MouseMiddle) {
            input.mi.dwFlags = isDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        }

        SendInput(1, &input, sizeof(INPUT));
        return;
    }

    // --- 2. HANDLE KEYBOARD INPUTS ---
    WORD vkCode = TranslateToWindowsVK(key);
    if (vkCode == 0) return;

    input.type = INPUT_KEYBOARD;
    input.mi.dwFlags = 0;
    input.ki.wVk = vkCode;

    if (!isDown) {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    }

    SendInput(1, &input, sizeof(INPUT));
}

// ==========================================
// MACOS IMPLEMENTATION
// ==========================================
#elif defined(__APPLE__)

CGKeyCode OSInputSimulator::TranslateToMacVK(ImGuiKey key) {
    switch (key) {
        // --- Arrows ---
    case ImGuiKey_UpArrow: return kVK_UpArrow;     case ImGuiKey_DownArrow: return kVK_DownArrow;
    case ImGuiKey_LeftArrow: return kVK_LeftArrow; case ImGuiKey_RightArrow: return kVK_RightArrow;

        // --- Numbers ---
    case ImGuiKey_0: return kVK_ANSI_0; case ImGuiKey_1: return kVK_ANSI_1; case ImGuiKey_2: return kVK_ANSI_2;
    case ImGuiKey_3: return kVK_ANSI_3; case ImGuiKey_4: return kVK_ANSI_4; case ImGuiKey_5: return kVK_ANSI_5;
    case ImGuiKey_6: return kVK_ANSI_6; case ImGuiKey_7: return kVK_ANSI_7; case ImGuiKey_8: return kVK_ANSI_8;
    case ImGuiKey_9: return kVK_ANSI_9;

        // --- Numpad ---
    case ImGuiKey_Keypad0: return kVK_ANSI_Keypad0; case ImGuiKey_Keypad1: return kVK_ANSI_Keypad1;
    case ImGuiKey_Keypad2: return kVK_ANSI_Keypad2; case ImGuiKey_Keypad3: return kVK_ANSI_Keypad3;
    case ImGuiKey_Keypad4: return kVK_ANSI_Keypad4; case ImGuiKey_Keypad5: return kVK_ANSI_Keypad5;
    case ImGuiKey_Keypad6: return kVK_ANSI_Keypad6; case ImGuiKey_Keypad7: return kVK_ANSI_Keypad7;
    case ImGuiKey_Keypad8: return kVK_ANSI_Keypad8; case ImGuiKey_Keypad9: return kVK_ANSI_Keypad9;

        // --- Alphabet ---
    case ImGuiKey_A: return kVK_ANSI_A; case ImGuiKey_B: return kVK_ANSI_B; case ImGuiKey_C: return kVK_ANSI_C;
    case ImGuiKey_D: return kVK_ANSI_D; case ImGuiKey_E: return kVK_ANSI_E; case ImGuiKey_F: return kVK_ANSI_F;
    case ImGuiKey_G: return kVK_ANSI_G; case ImGuiKey_H: return kVK_ANSI_H; case ImGuiKey_I: return kVK_ANSI_I;
    case ImGuiKey_J: return kVK_ANSI_J; case ImGuiKey_K: return kVK_ANSI_K; case ImGuiKey_L: return kVK_ANSI_L;
    case ImGuiKey_M: return kVK_ANSI_M; case ImGuiKey_N: return kVK_ANSI_N; case ImGuiKey_O: return kVK_ANSI_O;
    case ImGuiKey_P: return kVK_ANSI_P; case ImGuiKey_Q: return kVK_ANSI_Q; case ImGuiKey_R: return kVK_ANSI_R;
    case ImGuiKey_S: return kVK_ANSI_S; case ImGuiKey_T: return kVK_ANSI_T; case ImGuiKey_U: return kVK_ANSI_U;
    case ImGuiKey_V: return kVK_ANSI_V; case ImGuiKey_W: return kVK_ANSI_W; case ImGuiKey_X: return kVK_ANSI_X;
    case ImGuiKey_Y: return kVK_ANSI_Y; case ImGuiKey_Z: return kVK_ANSI_Z;

    default: return 0xFFFF; // Unmapped/Invalid
    }
}

void OSInputSimulator::SendHardwareKey(ImGuiKey key, bool isDown) {
    // --- 1. HANDLE MOUSE INPUTS ---
    if (key == ImGuiKey_MouseLeft || key == ImGuiKey_MouseRight || key == ImGuiKey_MouseMiddle) {
        CGEventType eventType;
        CGMouseButton mouseButton;

        if (key == ImGuiKey_MouseLeft) {
            eventType = isDown ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
            mouseButton = kCGMouseButtonLeft;
        }
        else if (key == ImGuiKey_MouseRight) {
            eventType = isDown ? kCGEventRightMouseDown : kCGEventRightMouseUp;
            mouseButton = kCGMouseButtonRight;
        }
        else if (key == ImGuiKey_MouseMiddle) {
            eventType = isDown ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
            mouseButton = kCGMouseButtonCenter;
        }

        // Get the current cursor location so we click in the right place
        CGEventRef locEvent = CGEventCreate(NULL);
        CGPoint cursorLoc = CGEventGetLocation(locEvent);
        CFRelease(locEvent);

        // Create and post the mouse event
        CGEventRef clickEvent = CGEventCreateMouseEvent(NULL, eventType, cursorLoc, mouseButton);
        CGEventPost(kCGHIDEventTap, clickEvent);
        CFRelease(clickEvent);
        return;
    }

    // --- 2. HANDLE KEYBOARD INPUTS ---
    CGKeyCode macCode = TranslateToMacVK(key);
    if (macCode == 0xFFFF) return;

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef event = CGEventCreateKeyboardEvent(source, macCode, isDown);

    CGEventPost(kCGHIDEventTap, event);

    CFRelease(event);
    CFRelease(source);
}

#endif

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

