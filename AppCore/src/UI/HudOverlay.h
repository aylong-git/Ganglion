#pragma once

#include "SSVEPTargets.h"

struct GLFWwindow;

// Top-left of each flicker square, as a fraction of the SSVEP panel
// (and of the HUD monitor).
struct SSVEPBoardLayout {
    float nx[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float ny[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float sizeFrac[4] = { 0.10f, 0.10f, 0.10f, 0.10f }; // each square as a fraction of monitor width
    bool visible[4] = { true, true, true, true };
    float customHz[4] = {
        kSSVEPTargetHz[0],
        kSSVEPTargetHz[1],
        kSSVEPTargetHz[2],
        kSSVEPTargetHz[3]
    };
    // 0 = plain board, 1 = QR code, 2 = checkerboard.
    int patternMode = 1;
    bool contrastMode = true;
    bool smoothMode = true;
};

// Plain boards only brighten and dim. QR and checkerboard can also reverse
// black and white, which doubles the SSVEP frequency the brain sees.
inline bool SSVEPPatternHasContrast(int patternMode)
{
    return patternMode == 1 || patternMode == 2;
}

class HudOverlay {
public:
    bool Enter(GLFWwindow* shareContext, const SSVEPBoardLayout& layout);
    void Leave();
    bool IsActive() const { return m_boards[0] != nullptr; }
    void Sync(const SSVEPBoardLayout& layout);
    // swapInterval is how many vblanks the pacing board waits. The other boards present immediately.
    void Render(int swapInterval);

private:
    bool InitGl();
    void DestroyGl();
    void BindBoard(int index);
    void DrawSolid(float x0, float y0, float x1, float y1, float r, float g, float b, float a);
    void DrawTexture(unsigned tex, float x0, float y0, float x1, float y1, float tint = 1.0f, int mode = 2);
    void DrawCaption(const char* text, float cx, float cy, float maxW, float maxH);

    float m_customHz[4] = {
        kSSVEPTargetHz[0],
        kSSVEPTargetHz[1],
        kSSVEPTargetHz[2],
        kSSVEPTargetHz[3]
    };
    GLFWwindow* m_boards[4] = {};
    unsigned m_vao[4] = {};
    bool m_visible[4] = { true, true, true, true };
    GLFWwindow* m_share = nullptr;
    unsigned long long m_frame = 0;
    int m_refreshHz = 60;
    int m_appliedSwap = -1;
    int m_vsyncBoard = -2;
    int m_patch[4] = {};
    int m_winH[4] = {};

    unsigned m_prog = 0;
    unsigned m_vbo = 0;
    int m_locMode = -1;
    int m_locColor = -1;
    int m_locTex = -1;

    unsigned m_texShort = 0;
    unsigned m_texLong = 0;
    unsigned m_texMouse[3] = {};
    int m_texShortW = 0, m_texShortH = 0;
    int m_texLongW = 0, m_texLongH = 0;
    int m_texMouseW[3] = {};
    int m_texMouseH[3] = {};
    unsigned m_texFont = 0;
    unsigned m_texWhite = 0;
    void* m_fontChars = nullptr; // stbtt_bakedchar[96]
    int m_fontAtlas = 512;
    float m_fontBakePx = 64.0f;

    int m_patternMode = 1;
    bool m_contrastMode = true;
    bool m_smoothMode = true;
    unsigned m_texQRCode = 0;
    unsigned m_texCheckerboard = 0;
};

void HudOverlayApplyPlatformNoActivate(GLFWwindow* window, GLFWwindow* ownerWindow = nullptr);
void HudOverlayKeepOnTop(GLFWwindow* window);

// Show the flicker square, plus cartoon pixels whose alpha is set.
// The empty band under the square stays outside the window.
void HudOverlaySetVisibleRegion(GLFWwindow* window, int boardIndex, int square,
    int iconX, int iconY, int iconW, int iconH,
    const unsigned char* opaque, int srcW, int srcH);
