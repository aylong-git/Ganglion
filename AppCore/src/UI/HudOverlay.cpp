#include "HudOverlay.h"
#include "SSVEPTargets.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "InputManager.h"
#include "KeyIconCatalog.h"
#include "Utils.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
void HudOverlayApplyPlatformNoActivate(GLFWwindow*, GLFWwindow*) {}
void HudOverlayKeepOnTop(GLFWwindow*) {}
void HudOverlayShowNoActivate(GLFWwindow* window) {
    if (window) glfwShowWindow(window);
}
void HudOverlaySetVisibleRegion(GLFWwindow*, int, int, int, int, int, int, const unsigned char*, int, int) {}
#endif

namespace {
constexpr int kIconGapPx = 12;
constexpr float kIconWidthFrac = 0.50f;

const char* kHudVert = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kHudFrag = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform int uMode;

// Pin pattern pixels to black or white. Gamma below 1 leaves the
// midpoint fixed and stretches everything else to the extremes.
vec3 expandBW(vec3 c) {
    const float g = 0.10;
    vec3 t = clamp(c, 0.0, 1.0) * 2.0 - 1.0;
    return sign(t) * pow(abs(t), vec3(g)) * 0.5 + 0.5;
}

void main() {
    vec4 color;
    if (uMode == 1) {
        float a = texture(uTex, vUV).r * uColor.a;
        color = vec4(uColor.rgb, a);
    } else if (uMode == 2) {
        color = texture(uTex, vUV) * uColor;
    } else if (uMode == 3) {
        // Invert the texture colors (1.0 - RGB)
        vec4 tex = texture(uTex, vUV);
        color = vec4(1.0 - tex.r, 1.0 - tex.g, 1.0 - tex.b, tex.a) * uColor;
    } else if (uMode == 4) {
        // Single-pass crossfade for pattern reversal.
        // uColor.r is the mix (0 = inverted, 1 = normal).
        vec3 tex = expandBW(texture(uTex, vUV).rgb);
        vec3 inv = vec3(1.0) - tex;
        vec3 mixed = mix(inv, tex, uColor.r);
        color = vec4(mixed, 1.0);
    } else if (uMode == 6) {
        // Outer ring and inner disk. uColor.r is the board signal.
        // >= 0.5: outer blue, inner red. Otherwise they swap.
        // Black strokes: both circles, plus spokes at the four sides.
        vec2 d = vUV - vec2(0.5);
        float r2 = dot(d, d);
        if (r2 > 0.25) discard;
        float rad = sqrt(r2);
        const float halfW = 0.03;
        const float outerEdge = 0.5 - halfW;
        bool normal = uColor.r >= 0.5;
        vec3 red = vec3(1.0, 0.0, 0.0);
        vec3 blue = vec3(0.0, 0.0, 1.0);
        bool inner = r2 <= 0.0625;
        bool onOuter = abs(rad - outerEdge) <= halfW;
        bool onInner = abs(rad - 0.25) <= halfW;
        bool spoke = rad >= 0.25 && rad <= outerEdge + halfW
            && (abs(d.x) <= halfW || abs(d.y) <= halfW);
        vec3 rgb = (onOuter || onInner || spoke)
            ? vec3(0.0)
            : (inner ? (normal ? red : blue) : (normal ? blue : red));
        color = vec4(rgb, 1.0);
    } else if (uMode == 5) {
        // Pattern brightness flicker with black/white gamma, no reversal.
        vec3 tex = expandBW(texture(uTex, vUV).rgb);
        color = vec4(tex * uColor.rgb, 1.0);
    } else {
        color = uColor;
    }
    // The Windows compositor reads premultiplied alpha.
    FragColor = vec4(color.rgb * color.a, color.a);
}
)";

GLFWmonitor* MonitorContainingWindow(GLFWwindow* window) {
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    const int cx = wx + ww / 2;
    const int cy = wy + wh / 2;

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;
        if (cx >= mx && cx < mx + mode->width && cy >= my && cy < my + mode->height) {
            return monitors[i];
        }
    }
    return glfwGetPrimaryMonitor();
}

void ApplyHudWindowHints() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifdef GLFW_MOUSE_PASSTHROUGH
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
}

unsigned CompileShader(GLenum type, const char* src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "[HUD] shader compile: " << log << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void ComputeHudWindowSize(float sizeFrac, int monitorW, int& patch, int& winH, bool withIcon = true) {
    const float frac = std::clamp(sizeFrac, 0.04f, 0.30f);
    patch = std::max(8, static_cast<int>(std::lround(static_cast<double>(monitorW) * frac)));
    if (!withIcon) {
        winH = patch;
        return;
    }
    int longW = 0, longH = 0, ch = 0;
    if (!stbi_info(GetAssetPath("keys/longkey.png").c_str(), &longW, &longH, &ch) || longW <= 0) {
        longW = 388;
        longH = 244;
    }
    const int iconH = std::max(8, static_cast<int>(std::lround(
        kIconWidthFrac * patch * (static_cast<float>(longH) / static_cast<float>(longW)))));
    winH = patch + kIconGapPx + iconH;
}

struct IconMask {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> opaque;
};

IconMask g_maskShort;
IconMask g_maskLong;
IconMask g_maskMouse[3];

void FillIconMask(IconMask& mask, const unsigned char* rgba, int w, int h) {
    mask.w = w;
    mask.h = h;
    mask.opaque.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    if (!rgba || w <= 0 || h <= 0) return;
    for (int i = 0; i < w * h; ++i) {
        mask.opaque[static_cast<size_t>(i)] = (rgba[i * 4 + 3] > 16) ? 1 : 0;
    }
}

unsigned LoadRgbaTexture(const char* relativeUnderAssets, int* outW, int* outH, IconMask* mask) {
    const std::string path = GetAssetPath(relativeUnderAssets);
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "[HUD] Failed to load " << path << "\n";
        return 0;
    }
    if (mask) FillIconMask(*mask, data, w, h);
    if (outW) *outW = w;
    if (outH) *outH = h;
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    return tex;
}
} // namespace

bool HudOverlay::Enter(GLFWwindow* shareContext, const SSVEPBoardLayout& layout) {
    if (IsActive()) return true;
    if (!shareContext) return false;

    m_share = shareContext;
    GLFWmonitor* monitor = MonitorContainingWindow(shareContext);
    if (!monitor) return false;

    int mx = 0, my = 0;
    glfwGetMonitorPos(monitor, &mx, &my);
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return false;

    m_refreshHz = (mode->refreshRate > 0) ? mode->refreshRate : 60;

    int posXY[kSSVEPBoardCount][2];
    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        ComputeHudWindowSize(layout.sizeFrac[i], mode->width, m_patch[i], m_winH[i], !SSVEPBoardIsNull(i));
        int x = mx + static_cast<int>(std::lround(layout.nx[i] * mode->width));
        int y = my + static_cast<int>(std::lround(layout.ny[i] * mode->height));
        const int xMax = mx + mode->width - m_patch[i];
        const int yMax = my + mode->height - m_winH[i];
        if (xMax >= mx) x = std::clamp(x, mx, xMax);
        if (yMax >= my) y = std::clamp(y, my, yMax);
        posXY[i][0] = x;
        posXY[i][1] = y;
    }

    ApplyHudWindowHints();

    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        ApplyHudWindowHints();
        m_boards[i] = glfwCreateWindow(m_patch[i], m_winH[i], "", nullptr, shareContext);
        if (!m_boards[i]) {
            std::cerr << "[HUD] Failed to create flicker board " << i << "\n";
            glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
            Leave();
            return false;
        }
        glfwSetWindowPos(m_boards[i], posXY[i][0], posXY[i][1]);
        glfwSetWindowAttrib(m_boards[i], GLFW_FLOATING, GLFW_TRUE);
        glfwSetWindowAttrib(m_boards[i], GLFW_AUTO_ICONIFY, GLFW_FALSE);
#ifdef GLFW_MOUSE_PASSTHROUGH
        glfwSetWindowAttrib(m_boards[i], GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
        glfwMakeContextCurrent(m_boards[i]);
        glfwSwapInterval(0);
        // Clip to the flicker square before the window is shown. The extra
        // height is only a slot for the cartoon; until it is clipped, Windows
        // paints that empty slot as a black rectangle.
        HudOverlayApplyPlatformNoActivate(m_boards[i], nullptr);
        HudOverlaySetVisibleRegion(m_boards[i], i, m_patch[i], 0, 0, 0, 0, nullptr, 0, 0);
        m_visible[i] = layout.visible[i];
        if (m_visible[i]) {
            HudOverlayShowNoActivate(m_boards[i]);
        }
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    if (!InitGl()) {
        Leave();
        return false;
    }

    m_patternMode = layout.patternMode;
    m_contrastMode = layout.contrastMode;
    m_smoothMode = layout.smoothMode;
    for (int i = 0; i < kSSVEPFlickerBoardCount; ++i) {
        m_customHz[i] = layout.customHz[i];
    }

    m_frame = 0;
    m_appliedSwap = -1;
    m_vsyncBoard = -2;
    glfwMakeContextCurrent(m_share);
    return true;
}

void HudOverlay::Leave() {
    DestroyGl();
    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        if (m_boards[i]) {
            glfwDestroyWindow(m_boards[i]);
            m_boards[i] = nullptr;
        }
    }
    m_frame = 0;
    m_appliedSwap = -1;
    m_vsyncBoard = -2;
    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        m_patch[i] = 0;
        m_winH[i] = 0;
    }
    for (int i = 0; i < kSSVEPBoardCount; ++i) m_visible[i] = true;
    if (m_share) {
        glfwMakeContextCurrent(m_share);
    }
    m_share = nullptr;
}

void HudOverlay::Sync(const SSVEPBoardLayout& layout) {
    if (!IsActive() || !m_share) return;

    GLFWmonitor* monitor = MonitorContainingWindow(m_share);
    if (!monitor) return;

    int mx = 0, my = 0;
    glfwGetMonitorPos(monitor, &mx, &my);
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return;

    m_patternMode = layout.patternMode;
    m_contrastMode = layout.contrastMode;
    m_smoothMode = layout.smoothMode;

    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        if (!m_boards[i]) continue;

        if (!SSVEPBoardIsNull(i)) {
            m_customHz[i] = layout.customHz[i];
        }

        int patch = 0;
        int winH = 0;
        ComputeHudWindowSize(layout.sizeFrac[i], mode->width, patch, winH, !SSVEPBoardIsNull(i));
        const bool sizeChanged = (patch != m_patch[i]) || (winH != m_winH[i]);
        m_patch[i] = patch;
        m_winH[i] = winH;

        int x = mx + static_cast<int>(std::lround(layout.nx[i] * mode->width));
        int y = my + static_cast<int>(std::lround(layout.ny[i] * mode->height));
        const int xMax = mx + mode->width - patch;
        const int yMax = my + mode->height - winH;
        if (xMax >= mx) x = std::clamp(x, mx, xMax);
        if (yMax >= my) y = std::clamp(y, my, yMax);
        int curX = 0, curY = 0;
        glfwGetWindowPos(m_boards[i], &curX, &curY);
        if (curX != x || curY != y) {
            glfwSetWindowPos(m_boards[i], x, y);
        }
        if (sizeChanged) {
            glfwSetWindowSize(m_boards[i], patch, winH);
            // Resizing restores a rectangular window, which brings the black
            // icon slot back. Clip to the square again until the next frame
            // adds the cartoon's opaque pixels.
            HudOverlaySetVisibleRegion(m_boards[i], i, patch, 0, 0, 0, 0, nullptr, 0, 0);
        }

        const bool wantVisible = layout.visible[i];
        if (wantVisible != m_visible[i]) {
            m_visible[i] = wantVisible;
            if (wantVisible) {
                glfwSetWindowAttrib(m_boards[i], GLFW_FLOATING, GLFW_TRUE);
#ifdef GLFW_MOUSE_PASSTHROUGH
                glfwSetWindowAttrib(m_boards[i], GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
                HudOverlayApplyPlatformNoActivate(m_boards[i], nullptr);
                HudOverlaySetVisibleRegion(m_boards[i], i, patch, 0, 0, 0, 0, nullptr, 0, 0);
                HudOverlayShowNoActivate(m_boards[i]);
            }
            else {
                glfwHideWindow(m_boards[i]);
            }
        }
        else {
            m_visible[i] = wantVisible;
        }
    }
}

bool HudOverlay::InitGl() {
    glfwMakeContextCurrent(m_boards[0]);

    unsigned vs = CompileShader(GL_VERTEX_SHADER, kHudVert);
    unsigned fs = CompileShader(GL_FRAGMENT_SHADER, kHudFrag);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int linked = 0;
    glGetProgramiv(m_prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512] = {};
        glGetProgramInfoLog(m_prog, 512, nullptr, log);
        std::cerr << "[HUD] shader link: " << log << "\n";
        glDeleteProgram(m_prog);
        m_prog = 0;
        return false;
    }
    m_locMode = glGetUniformLocation(m_prog, "uMode");
    m_locColor = glGetUniformLocation(m_prog, "uColor");
    m_locTex = glGetUniformLocation(m_prog, "uTex");

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_STREAM_DRAW);

    m_texShort = LoadRgbaTexture("keys/shortkey.png", &m_texShortW, &m_texShortH, &g_maskShort);
    m_texLong = LoadRgbaTexture("keys/longkey.png", &m_texLongW, &m_texLongH, &g_maskLong);
    m_texMouse[0] = LoadRgbaTexture("keys/mouse_1.png", &m_texMouseW[0], &m_texMouseH[0], &g_maskMouse[0]);
    m_texMouse[1] = LoadRgbaTexture("keys/mouse_2.png", &m_texMouseW[1], &m_texMouseH[1], &g_maskMouse[1]);
    m_texMouse[2] = LoadRgbaTexture("keys/mouse_3.png", &m_texMouseW[2], &m_texMouseH[2], &g_maskMouse[2]);
    m_texQRCode = LoadRgbaTexture("qr_code.png", nullptr, nullptr, nullptr);
    m_texCheckerboard = LoadRgbaTexture("checkerboard.png", nullptr, nullptr, nullptr);

    {
        unsigned char px[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &m_texWhite);
        glBindTexture(GL_TEXTURE_2D, m_texWhite);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    const std::string fontPath = GetAssetPath("Nunito-ExtraBold.ttf");
    std::ifstream fontFile(fontPath, std::ios::binary);
    if (fontFile) {
        std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(fontFile)),
            std::istreambuf_iterator<char>());
        auto* baked = new stbtt_bakedchar[96];
        std::vector<unsigned char> atlas(m_fontAtlas * m_fontAtlas, 0);
        const int bake = stbtt_BakeFontBitmap(ttf.data(), 0, m_fontBakePx,
            atlas.data(), m_fontAtlas, m_fontAtlas, 32, 96, baked);
        if (bake > 0) {
#if defined(_WIN32)
            // Stamp U+2190..U+2193 from Segoe UI into the unused part of the atlas.
            // Nunito does not contain these short arrows.
            char winDir[MAX_PATH] = {};
            if (GetWindowsDirectoryA(winDir, MAX_PATH) > 0) {
                const std::string arrowPath = std::string(winDir) + "\\Fonts\\segoeui.ttf";
                std::ifstream arrowFile(arrowPath, std::ios::binary);
                if (arrowFile) {
                    std::vector<unsigned char> arrowTtf((std::istreambuf_iterator<char>(arrowFile)),
                        std::istreambuf_iterator<char>());
                    stbtt_fontinfo arrowInfo;
                    const int arrowOffset = stbtt_GetFontOffsetForIndex(arrowTtf.data(), 0);
                    if (!arrowTtf.empty() && stbtt_InitFont(&arrowInfo, arrowTtf.data(), arrowOffset)) {
                        auto* arrows = new stbtt_bakedchar[4]{};
                        const float arrowScale = stbtt_ScaleForPixelHeight(&arrowInfo, m_fontBakePx);
                        int penX = 1;
                        int penY = bake + 1;
                        int rowH = 0;
                        bool any = false;
                        for (int i = 0; i < 4; ++i) {
                            const int cp = 0x2190 + i;
                            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                            stbtt_GetCodepointBitmapBox(&arrowInfo, cp, arrowScale, arrowScale, &x0, &y0, &x1, &y1);
                            const int gw = x1 - x0;
                            const int gh = y1 - y0;
                            int advance = 0, lsb = 0;
                            stbtt_GetCodepointHMetrics(&arrowInfo, cp, &advance, &lsb);
                            if (gw <= 0 || gh <= 0 || penY + gh >= m_fontAtlas) {
                                m_arrowReady[i] = false;
                                continue;
                            }
                            if (penX + gw + 1 >= m_fontAtlas) {
                                penX = 1;
                                penY += rowH + 1;
                                rowH = 0;
                                if (penY + gh >= m_fontAtlas) {
                                    m_arrowReady[i] = false;
                                    continue;
                                }
                            }
                            stbtt_MakeCodepointBitmap(&arrowInfo,
                                atlas.data() + penY * m_fontAtlas + penX,
                                gw, gh, m_fontAtlas, arrowScale, arrowScale, cp);
                            arrows[i].x0 = static_cast<unsigned short>(penX);
                            arrows[i].y0 = static_cast<unsigned short>(penY);
                            arrows[i].x1 = static_cast<unsigned short>(penX + gw);
                            arrows[i].y1 = static_cast<unsigned short>(penY + gh);
                            arrows[i].xoff = static_cast<float>(x0);
                            arrows[i].yoff = static_cast<float>(y0);
                            arrows[i].xadvance = arrowScale * static_cast<float>(advance);
                            penX += gw + 2;
                            if (gh > rowH) rowH = gh;
                            m_arrowReady[i] = true;
                            any = true;
                        }
                        if (any) m_arrowGlyph = arrows;
                        else delete[] arrows;
                    }
                }
            }
#endif
            glGenTextures(1, &m_texFont);
            glBindTexture(GL_TEXTURE_2D, m_texFont);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_fontAtlas, m_fontAtlas, 0,
                GL_RED, GL_UNSIGNED_BYTE, atlas.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_fontChars = baked;
        }
        else {
            delete[] baked;
            std::cerr << "[HUD] Font bake failed for " << fontPath << "\n";
        }
    }
    else {
        std::cerr << "[HUD] Missing font " << fontPath << "\n";
    }

    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        glfwMakeContextCurrent(m_boards[i]);
        glGenVertexArrays(1, &m_vao[i]);
        glBindVertexArray(m_vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
    return true;
}

void HudOverlay::DestroyGl() {
    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        if (m_vao[i] && m_boards[i]) {
            glfwMakeContextCurrent(m_boards[i]);
            glDeleteVertexArrays(1, &m_vao[i]);
            m_vao[i] = 0;
        }
    }
    if (m_boards[0]) glfwMakeContextCurrent(m_boards[0]);

    auto delTex = [](unsigned& t) {
        if (t) {
            glDeleteTextures(1, &t);
            t = 0;
        }
    };
    delTex(m_texShort);
    delTex(m_texLong);
    delTex(m_texMouse[0]);
    delTex(m_texMouse[1]);
    delTex(m_texMouse[2]);
    delTex(m_texQRCode);
    delTex(m_texCheckerboard);
    delTex(m_texFont);
    delTex(m_texWhite);
    m_texShortW = m_texShortH = m_texLongW = m_texLongH = 0;
    m_texMouseW[0] = m_texMouseW[1] = m_texMouseW[2] = 0;
    m_texMouseH[0] = m_texMouseH[1] = m_texMouseH[2] = 0;
    if (m_fontChars) {
        delete[] static_cast<stbtt_bakedchar*>(m_fontChars);
        m_fontChars = nullptr;
    }
    if (m_arrowGlyph) {
        delete[] static_cast<stbtt_bakedchar*>(m_arrowGlyph);
        m_arrowGlyph = nullptr;
    }
    m_arrowReady[0] = m_arrowReady[1] = m_arrowReady[2] = m_arrowReady[3] = false;
    if (m_prog) {
        glDeleteProgram(m_prog);
        m_prog = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void HudOverlay::BindBoard(int index) {
    glBindVertexArray(m_vao[index]);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glUseProgram(m_prog);
    glUniform1i(m_locTex, 0);
    glActiveTexture(GL_TEXTURE0);
}

void HudOverlay::DrawSolid(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return;
    auto X = [&](float x) { return x / static_cast<float>(fbW) * 2.0f - 1.0f; };
    auto Y = [&](float y) { return 1.0f - y / static_cast<float>(fbH) * 2.0f; };
    const float v[] = {
        X(x0), Y(y0), 0.0f, 0.0f,
        X(x0), Y(y1), 0.0f, 1.0f,
        X(x1), Y(y0), 1.0f, 0.0f,
        X(x1), Y(y1), 1.0f, 1.0f
    };
    glUseProgram(m_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texWhite);
    glUniform1i(m_locTex, 0);
    glUniform1i(m_locMode, 0);
    glUniform4f(m_locColor, r, g, b, a);
    glDisable(GL_BLEND);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void HudOverlay::DrawTexture(unsigned tex, float x0, float y0, float x1, float y1, float tint, int mode) {
    if (!tex) return;
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return;
    auto X = [&](float x) { return x / static_cast<float>(fbW) * 2.0f - 1.0f; };
    auto Y = [&](float y) { return 1.0f - y / static_cast<float>(fbH) * 2.0f; };
    const float v[] = {
        X(x0), Y(y0), 0.0f, 0.0f,
        X(x0), Y(y1), 0.0f, 1.0f,
        X(x1), Y(y0), 1.0f, 0.0f,
        X(x1), Y(y1), 1.0f, 1.0f
    };
    glUseProgram(m_prog);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glUniform1i(m_locMode, mode);
    glUniform4f(m_locColor, tint, tint, tint, 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void HudOverlay::DrawCaption(const char* text, float cx, float cy, float maxW, float maxH) {
    if (!text || !text[0] || !m_fontChars || !m_texFont) return;
    auto* baked = static_cast<stbtt_bakedchar*>(m_fontChars);
    auto* arrows = static_cast<stbtt_bakedchar*>(m_arrowGlyph);

    auto nextCp = [](const char*& p) -> int {
        const unsigned char c0 = static_cast<unsigned char>(*p);
        if (c0 == 0) return 0;
        if (c0 < 0x80) {
            ++p;
            return c0;
        }
        if ((c0 & 0xF0) == 0xE0 && p[1] && p[2]) {
            const int cp = ((c0 & 0x0F) << 12)
                | ((static_cast<unsigned char>(p[1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(p[2]) & 0x3F);
            p += 3;
            return cp;
        }
        ++p;
        return -1;
    };
    auto glyphFor = [&](int cp, stbtt_bakedchar& out) -> bool {
        if (cp >= 32 && cp < 128) {
            out = baked[cp - 32];
            return true;
        }
        if (arrows && cp >= 0x2190 && cp <= 0x2193 && m_arrowReady[cp - 0x2190]) {
            out = arrows[cp - 0x2190];
            return true;
        }
        return false;
    };

    float x = 0.0f, y = 0.0f;
    float y0 = 1.0e9f, y1 = -1.0e9f;
    for (const char* p = text; *p; ) {
        const int cp = nextCp(p);
        stbtt_bakedchar g;
        if (cp <= 0 || !glyphFor(cp, g)) continue;
        stbtt_aligned_quad q;
        float qx = x, qy = y;
        stbtt_GetBakedQuad(&g, m_fontAtlas, m_fontAtlas, 0, &qx, &qy, &q, 1);
        x += g.xadvance;
        y0 = std::min(y0, q.y0);
        y1 = std::max(y1, q.y1);
    }
    if (x <= 0.0f || y1 <= y0) return;

    const float scale = std::min(maxW / x, maxH / (y1 - y0));
    const float originX = cx - 0.5f * x * scale;
    const float originY = cy - 0.5f * (y0 + y1) * scale;

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return;
    auto X = [&](float px) { return px / static_cast<float>(fbW) * 2.0f - 1.0f; };
    auto Y = [&](float py) { return 1.0f - py / static_cast<float>(fbH) * 2.0f; };

    glBindTexture(GL_TEXTURE_2D, m_texFont);
    glUniform1i(m_locMode, 1);
    glUniform4f(m_locColor, 0.10f, 0.10f, 0.10f, 1.0f);

    x = 0.0f;
    y = 0.0f;
    for (const char* p = text; *p; ) {
        const int cp = nextCp(p);
        stbtt_bakedchar g;
        if (cp <= 0 || !glyphFor(cp, g)) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(&g, m_fontAtlas, m_fontAtlas, 0, &x, &y, &q, 1);
        const float gx0 = originX + q.x0 * scale;
        const float gy0 = originY + q.y0 * scale;
        const float gx1 = originX + q.x1 * scale;
        const float gy1 = originY + q.y1 * scale;
        const float v[] = {
            X(gx0), Y(gy0), q.s0, q.t0,
            X(gx0), Y(gy1), q.s0, q.t1,
            X(gx1), Y(gy0), q.s1, q.t0,
            X(gx1), Y(gy1), q.s1, q.t1
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void HudOverlay::Render(int swapInterval) {
    if (!IsActive() || !m_prog) return;
    if (swapInterval < 1) swapInterval = 1;

    int vsyncBoard = -1;
    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        if (m_boards[i] && m_visible[i]) vsyncBoard = i;
    }

    // One board blocks on the display. The others present without waiting,
    // so four windows do not stack four vblank waits.
    if (swapInterval != m_appliedSwap || vsyncBoard != m_vsyncBoard) {
        m_appliedSwap = swapInterval;
        m_vsyncBoard = vsyncBoard;
        for (int i = 0; i < kSSVEPBoardCount; ++i) {
            if (!m_boards[i] || !m_visible[i]) continue;
            glfwMakeContextCurrent(m_boards[i]);
            glfwSwapInterval(i == vsyncBoard ? swapInterval : 0);
        }
    }

    auto& inputMgr = InputManager::GetInstance();

    const double timeNow = glfwGetTime();

    for (int i = 0; i < kSSVEPBoardCount; ++i) {
        if (!m_boards[i] || !m_visible[i]) continue;
        glfwSetWindowShouldClose(m_boards[i], GLFW_FALSE);
        glfwMakeContextCurrent(m_boards[i]);

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(m_boards[i], &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        BindBoard(i);
        const bool nullBoard = SSVEPBoardIsNull(i);
        const bool contrast = !nullBoard && SSVEPPatternHasContrast(m_patternMode) && m_contrastMode;
        const float screenTarget = nullBoard ? 0.0f : (contrast ? (m_customHz[i] / 2.0f) : m_customHz[i]);

        const bool on = nullBoard ? true : SSVEPSquareOn(timeNow, screenTarget);

        const float alpha = nullBoard
            ? 1.0f
            : (m_smoothMode
                ? SSVEPSineAlpha(timeNow, screenTarget)
                : (on ? 1.0f : 0.0f));
            
        const float square = static_cast<float>(fbW);

        unsigned patternTex = 0;
        if (m_patternMode == 1) patternTex = m_texQRCode;
        else if (m_patternMode == 2) patternTex = m_texCheckerboard;

        if (patternTex) {
            if (contrast) {
                // Shader mode 4 crossfades the image with its color inverse.
                // alpha is the mix and is read from uColor.r.
                DrawTexture(patternTex, 0.0f, 0.0f, square, square, alpha, 4);
            } else {
                DrawTexture(patternTex, 0.0f, 0.0f, square, square, alpha, 5);
            }
        } else {
            // Plain board: alpha is the grayscale brightness.
            DrawSolid(0.0f, 0.0f, square, square, alpha, alpha, alpha, 1.0f);
        }

        const float fixRadius = square * kSSVEPFixationDiameterFrac * 0.5f;
        const float fixCx = square * 0.5f;
        const float fixCy = square * 0.5f;
        DrawTexture(m_texWhite, fixCx - fixRadius, fixCy - fixRadius,
            fixCx + fixRadius, fixCy + fixRadius, alpha, 6);

        const ImGuiKey key = nullBoard ? ImGuiKey_None : inputMgr.GetKey(i);
        const auto kind = KeyIconCatalog::KindFor(key);
        unsigned tex = 0;
        switch (kind) {
        case KeyIconCatalog::Kind::ShortKey:    tex = m_texShort; break;
        case KeyIconCatalog::Kind::LongKey:     tex = m_texLong; break;
        case KeyIconCatalog::Kind::MouseLeft:   tex = m_texMouse[0]; break;
        case KeyIconCatalog::Kind::MouseRight:  tex = m_texMouse[1]; break;
        case KeyIconCatalog::Kind::MouseMiddle: tex = m_texMouse[2]; break;
        default: break;
        }

        if (tex) {
            float srcW = 0.0f, srcH = 0.0f;
            switch (kind) {
            case KeyIconCatalog::Kind::ShortKey:
                srcW = static_cast<float>(m_texShortW); srcH = static_cast<float>(m_texShortH); break;
            case KeyIconCatalog::Kind::LongKey:
                srcW = static_cast<float>(m_texLongW); srcH = static_cast<float>(m_texLongH); break;
            case KeyIconCatalog::Kind::MouseLeft:
                srcW = static_cast<float>(m_texMouseW[0]); srcH = static_cast<float>(m_texMouseH[0]); break;
            case KeyIconCatalog::Kind::MouseRight:
                srcW = static_cast<float>(m_texMouseW[1]); srcH = static_cast<float>(m_texMouseH[1]); break;
            case KeyIconCatalog::Kind::MouseMiddle:
                srcW = static_cast<float>(m_texMouseW[2]); srcH = static_cast<float>(m_texMouseH[2]); break;
            default: break;
            }
            const float gap = kIconGapPx * square / static_cast<float>(std::max(1, m_patch[i]));
            float dw = 0.0f, dh = 0.0f;
            KeyIconCatalog::DisplaySize(srcW, srcH, square,
                static_cast<float>(m_texLongW), static_cast<float>(m_texLongH), dw, dh);
            const float dx = 0.5f * (square - dw);
            const float dy = square + gap;
            DrawTexture(tex, dx, dy, dx + dw, dy + dh);

            const IconMask* mask = nullptr;
            switch (kind) {
            case KeyIconCatalog::Kind::ShortKey:    mask = &g_maskShort; break;
            case KeyIconCatalog::Kind::LongKey:     mask = &g_maskLong; break;
            case KeyIconCatalog::Kind::MouseLeft:   mask = &g_maskMouse[0]; break;
            case KeyIconCatalog::Kind::MouseRight:  mask = &g_maskMouse[1]; break;
            case KeyIconCatalog::Kind::MouseMiddle: mask = &g_maskMouse[2]; break;
            default: break;
            }
            int ww = 0, wh = 0;
            glfwGetWindowSize(m_boards[i], &ww, &wh);
            const float sx = (fbW > 0) ? static_cast<float>(ww) / static_cast<float>(fbW) : 1.0f;
            const float sy = (fbH > 0) ? static_cast<float>(wh) / static_cast<float>(fbH) : 1.0f;
            HudOverlaySetVisibleRegion(m_boards[i], i, ww,
                static_cast<int>(std::lround(dx * sx)),
                static_cast<int>(std::lround(dy * sy)),
                static_cast<int>(std::lround(dw * sx)),
                static_cast<int>(std::lround(dh * sy)),
                (mask && !mask->opaque.empty()) ? mask->opaque.data() : nullptr,
                mask ? mask->w : 0,
                mask ? mask->h : 0);

            const std::string caption = KeyIconCatalog::Caption(key);
            if (!caption.empty()) {
                const bool longKey = (kind == KeyIconCatalog::Kind::LongKey);
                const float faceCx = dx + 0.5f * dw;
                const float faceCy = dy + dh * 0.38f;
                const float maxW = dw * (longKey ? 0.62f : 0.50f);
                const float maxH = dh * (longKey ? 0.22f : 0.34f);
                DrawCaption(caption.c_str(), faceCx, faceCy, maxW, maxH);
            }
        }
        else {
            int ww = 0, wh = 0;
            glfwGetWindowSize(m_boards[i], &ww, &wh);
            HudOverlaySetVisibleRegion(m_boards[i], i, ww, 0, 0, 0, 0, nullptr, 0, 0);
        }

        glBindVertexArray(0);
        glfwSwapBuffers(m_boards[i]);
    }

    ++m_frame;
    if (m_share) {
        glfwMakeContextCurrent(m_share);
    }
}
