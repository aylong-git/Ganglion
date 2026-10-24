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

#if !defined(_WIN32) && !defined(__APPLE__)
void HudOverlayApplyPlatformNoActivate(GLFWwindow*, GLFWwindow*) {}
void HudOverlayKeepOnTop(GLFWwindow*) {}
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
        vec4 tex = texture(uTex, vUV);
        vec3 inv = vec3(1.0 - tex.r, 1.0 - tex.g, 1.0 - tex.b);
        vec3 mixed = mix(inv, tex.rgb, uColor.r);
        color = vec4(mixed, tex.a);
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

void ComputeHudWindowSize(float sizeFrac, int monitorW, int& patch, int& winH) {
    const float frac = std::clamp(sizeFrac, 0.04f, 0.30f);
    patch = std::max(8, static_cast<int>(std::lround(static_cast<double>(monitorW) * frac)));
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

    int posXY[4][2];
    for (int i = 0; i < 4; ++i) {
        ComputeHudWindowSize(layout.sizeFrac[i], mode->width, m_patch[i], m_winH[i]);
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

    for (int i = 0; i < 4; ++i) {
        ApplyHudWindowHints();
        m_boards[i] = glfwCreateWindow(m_patch[i], m_winH[i], "", nullptr, shareContext);
        if (!m_boards[i]) {
            std::cerr << "[HUD] Failed to create flicker board " << i << "\n";
            Leave();
            return false;
        }
        glfwSetWindowPos(m_boards[i], posXY[i][0], posXY[i][1]);
        glfwSetWindowAttrib(m_boards[i], GLFW_FLOATING, GLFW_TRUE);
        glfwSetWindowAttrib(m_boards[i], GLFW_AUTO_ICONIFY, GLFW_FALSE);
#ifdef GLFW_MOUSE_PASSTHROUGH
        glfwSetWindowAttrib(m_boards[i], GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
        HudOverlayApplyPlatformNoActivate(m_boards[i], nullptr);

        m_visible[i] = layout.visible[i];
        if (!m_visible[i]) {
            glfwHideWindow(m_boards[i]);
        }

        glfwMakeContextCurrent(m_boards[i]);
        glfwSwapInterval(0);
    }

    if (!InitGl()) {
        Leave();
        return false;
    }

    m_frame = 0;
    m_waitVsync = false;
    glfwMakeContextCurrent(m_share);
    return true;
}

void HudOverlay::Leave() {
    DestroyGl();
    for (int i = 0; i < 4; ++i) {
        if (m_boards[i]) {
            glfwDestroyWindow(m_boards[i]);
            m_boards[i] = nullptr;
        }
    }
    m_frame = 0;
    m_waitVsync = false;
    for (int i = 0; i < 4; ++i) {
        m_patch[i] = 0;
        m_winH[i] = 0;
    }
    for (int i = 0; i < 4; ++i) m_visible[i] = true;
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

    for (int i = 0; i < 4; ++i) {
        if (!m_boards[i]) continue;

        m_customHz[i] = layout.customHz[i];

        int patch = 0;
        int winH = 0;
        ComputeHudWindowSize(layout.sizeFrac[i], mode->width, patch, winH);
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
        }

        const bool wantVisible = layout.visible[i];
        if (wantVisible != m_visible[i]) {
            m_visible[i] = wantVisible;
            if (wantVisible) {
                glfwShowWindow(m_boards[i]);
                glfwSetWindowAttrib(m_boards[i], GLFW_FLOATING, GLFW_TRUE);
#ifdef GLFW_MOUSE_PASSTHROUGH
                glfwSetWindowAttrib(m_boards[i], GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
                HudOverlayApplyPlatformNoActivate(m_boards[i], nullptr);
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

    for (int i = 0; i < 4; ++i) {
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
    for (int i = 0; i < 4; ++i) {
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
    delTex(m_texFont);
    delTex(m_texWhite);
    m_texShortW = m_texShortH = m_texLongW = m_texLongH = 0;
    m_texMouseW[0] = m_texMouseW[1] = m_texMouseW[2] = 0;
    m_texMouseH[0] = m_texMouseH[1] = m_texMouseH[2] = 0;
    if (m_fontChars) {
        delete[] static_cast<stbtt_bakedchar*>(m_fontChars);
        m_fontChars = nullptr;
    }
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

    float x = 0.0f, y = 0.0f;
    float y0 = 1.0e9f, y1 = -1.0e9f;
    for (const char* p = text; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c >= 128) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(baked, m_fontAtlas, m_fontAtlas, c - 32, &x, &y, &q, 1);
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
    for (const char* p = text; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c >= 128) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(baked, m_fontAtlas, m_fontAtlas, c - 32, &x, &y, &q, 1);
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

void HudOverlay::Render(bool waitForVsync) {
    if (!IsActive() || !m_prog) return;

    int vsyncBoard = -1;
    for (int i = 0; i < 4; ++i) {
        if (m_boards[i] && m_visible[i]) vsyncBoard = i;
    }

    if (waitForVsync != m_waitVsync) {
        m_waitVsync = waitForVsync;
        for (int i = 0; i < 4; ++i) {
            if (!m_boards[i] || !m_visible[i]) continue;
            glfwMakeContextCurrent(m_boards[i]);
            glfwSwapInterval((waitForVsync && i == vsyncBoard) ? 1 : 0);
        }
    }

    auto& inputMgr = InputManager::GetInstance();

    const double timeNow = glfwGetTime();

    for (int i = 0; i < 4; ++i) {
        if (!m_boards[i] || !m_visible[i]) continue;
        glfwSetWindowShouldClose(m_boards[i], GLFW_FALSE);
        HudOverlayKeepOnTop(m_boards[i]);
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
        const float screenTarget =
            (m_patternMode == 1 && m_contrastMode)
            ? (m_customHz[i] / 2.0f)
            : m_customHz[i];

        const bool on =
            SSVEPSquareOn(timeNow, screenTarget);

        const float alpha =
            m_smoothMode
            ? SSVEPSineAlpha(timeNow, screenTarget)
            : (on ? 1.0f : 0.0f);
            
        const float square = static_cast<float>(fbW);
        
        if (m_patternMode == 1 && m_texQRCode) {
            if (m_contrastMode) {
                // CHANGED: Use the new shader mode (4) for a perfect mathematical crossfade.
                // The 'alpha' variable is passed as the tint, which becomes uColor.r in the shader.
                DrawTexture(m_texQRCode, 0.0f, 0.0f, square, square, alpha, 4);
            } else {
                // Standard Tinting: Apply alpha to brightness
                DrawTexture(m_texQRCode, 0.0f, 0.0f, square, square, alpha, 2);
            }
        } else {
            // Plain Board: Alpha acts as RGB brightness
            DrawSolid(0.0f, 0.0f, square, square, alpha, alpha, alpha, 1.0f);
        }

        const ImGuiKey key = inputMgr.GetKey(i);
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
        if (!waitForVsync) {
            glfwSwapInterval(1);
        }
    }
}
