#include "KeyIconCatalog.h"
#include "Utils.h"

#include <cctype>
#include <cstring>

KeyIconCatalog::Kind KeyIconCatalog::KindFor(ImGuiKey key) {
    switch (key) {
    case ImGuiKey_None:
        return Kind::None;

    case ImGuiKey_MouseLeft:
        return Kind::MouseLeft;
    case ImGuiKey_MouseRight:
        return Kind::MouseRight;
    case ImGuiKey_MouseMiddle:
        return Kind::MouseMiddle;

    case ImGuiKey_CapsLock:
    case ImGuiKey_Backspace:
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter:
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl:
    case ImGuiKey_Space:
        return Kind::LongKey;

    default:
        return Kind::ShortKey;
    }
}

void KeyIconCatalog::DisplaySize(float srcW, float srcH, float boardW,
    float longW, float longH, float& dw, float& dh) {
    const float refW = (longW > 1.0f) ? longW : ((srcW > 1.0f) ? srcW : 1.0f);
    const float refH = (longH > 1.0f) ? longH : ((srcH > 1.0f) ? srcH : 1.0f);
    dh = boardW * 0.5f * (refH / refW);
    if (srcH > 1.0f) {
        dw = dh * (srcW / srcH);
    }
    else {
        dw = boardW * 0.5f;
    }
}

const char* KeyIconCatalog::RelativePath(ImGuiKey key) {
    switch (KindFor(key)) {
    case Kind::LongKey:      return "keys/longkey.png";
    case Kind::ShortKey:     return "keys/shortkey.png";
    case Kind::MouseLeft:    return "keys/mouse_1.png";
    case Kind::MouseRight:   return "keys/mouse_2.png";
    case Kind::MouseMiddle:  return "keys/mouse_3.png";
    case Kind::None:
    default:                 return "";
    }
}

std::string KeyIconCatalog::AbsolutePath(ImGuiKey key) {
    const char* rel = RelativePath(key);
    if (!rel || rel[0] == '\0') return {};
    return GetAssetPath(rel);
}

std::string KeyIconCatalog::Caption(ImGuiKey key) {
    switch (KindFor(key)) {
    case Kind::None:
    case Kind::MouseLeft:
    case Kind::MouseRight:
    case Kind::MouseMiddle:
        return {};
    default:
        break;
    }

    switch (key) {
    case ImGuiKey_Space: return "SPACE";
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift: return "SHIFT";
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl: return "CTRL";
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt: return "ALT";
    case ImGuiKey_LeftSuper:
    case ImGuiKey_RightSuper: return "WIN";
    case ImGuiKey_Enter:
    case ImGuiKey_KeypadEnter: return "ENTER";
    case ImGuiKey_CapsLock: return "CAPS";
    case ImGuiKey_Backspace: return "BKSP";
    case ImGuiKey_Delete: return "DEL";
    case ImGuiKey_Escape: return "ESC";
    case ImGuiKey_Tab: return "TAB";
    case ImGuiKey_Insert: return "INS";
    case ImGuiKey_Home: return "HOME";
    case ImGuiKey_End: return "END";
    case ImGuiKey_PageUp: return "PGUP";
    case ImGuiKey_PageDown: return "PGDN";
    case ImGuiKey_UpArrow: return "\xE2\x86\x91";    // U+2191 ↑
    case ImGuiKey_DownArrow: return "\xE2\x86\x93";  // U+2193 ↓
    case ImGuiKey_LeftArrow: return "\xE2\x86\x90";  // U+2190 ←
    case ImGuiKey_RightArrow: return "\xE2\x86\x92"; // U+2192 →
    case ImGuiKey_GraveAccent: return "`";
    case ImGuiKey_Minus: return "-";
    case ImGuiKey_Equal: return "=";
    case ImGuiKey_LeftBracket: return "[";
    case ImGuiKey_RightBracket: return "]";
    case ImGuiKey_Backslash: return "\\";
    case ImGuiKey_Semicolon: return ";";
    case ImGuiKey_Apostrophe: return "'";
    case ImGuiKey_Comma: return ",";
    case ImGuiKey_Period: return ".";
    case ImGuiKey_Slash: return "/";
    case ImGuiKey_Keypad0: return "0";
    case ImGuiKey_Keypad1: return "1";
    case ImGuiKey_Keypad2: return "2";
    case ImGuiKey_Keypad3: return "3";
    case ImGuiKey_Keypad4: return "4";
    case ImGuiKey_Keypad5: return "5";
    case ImGuiKey_Keypad6: return "6";
    case ImGuiKey_Keypad7: return "7";
    case ImGuiKey_Keypad8: return "8";
    case ImGuiKey_Keypad9: return "9";
    case ImGuiKey_KeypadDecimal: return ".";
    case ImGuiKey_KeypadDivide: return "/";
    case ImGuiKey_KeypadMultiply: return "*";
    case ImGuiKey_KeypadSubtract: return "-";
    case ImGuiKey_KeypadAdd: return "+";
    case ImGuiKey_KeypadEqual: return "=";
    default:
        break;
    }

    const char* raw = ImGui::GetKeyName(key);
    if (!raw || !raw[0] || std::strcmp(raw, "Unknown") == 0) return {};

    std::string s(raw);
    if (s.rfind("Keypad ", 0) == 0) s.erase(0, 7);
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}
