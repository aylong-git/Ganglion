#pragma once
#include <imgui.h>
#include <string>

// Maps a bound ImGuiKey to a file under AppCore/assets/keys/.
// InputManager stores the key; this catalog owns filenames.
class KeyIconCatalog {
public:
    enum class Kind {
        None,
        ShortKey,
        LongKey,
        MouseLeft,
        MouseRight,
        MouseMiddle
    };

    static Kind KindFor(ImGuiKey key);

    // Long-key art is 50% of the flicker board wide. Every cartoon is scaled
    // to that same height; width follows the PNG aspect (may exceed 50%).
    static void DisplaySize(float srcW, float srcH, float boardW,
        float longW, float longH, float& dw, float& dh);

    // Path relative to the assets root, e.g. "keys/longkey.png".
    // Empty string if the board is unbound.
    static const char* RelativePath(ImGuiKey key);

    static std::string AbsolutePath(ImGuiKey key);

    // Legend drawn on the keycap. Empty for unbound keys and mouse icons.
    static std::string Caption(ImGuiKey key);
};
