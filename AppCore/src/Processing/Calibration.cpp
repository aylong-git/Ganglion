#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Calibration.h"
#include "SSVEPClassifier.h"

#include "InputManager.h"
#include "KeyIconCatalog.h"
#include "Utils.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "stb_image.h"

#if defined(_WIN32)
#include <windows.h>
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
constexpr double kCountdownSec = 3.0;
constexpr double kPauseSec = 2.0;

const char* kVert = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kFrag = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform int uMode;
void main() {
    if (uMode == 1) {
        float a = texture(uTex, vUV).r * uColor.a;
        FragColor = vec4(uColor.rgb, a);
    } else if (uMode == 2) {
        FragColor = texture(uTex, vUV) * uColor;
    } else if (uMode == 4) {
        vec4 tex = texture(uTex, vUV);
        vec3 inv = vec3(1.0) - tex.rgb;
        FragColor = vec4(mix(inv, tex.rgb, uColor.r), 1.0);
    } else if (uMode == 6) {
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
        FragColor = vec4(rgb, 1.0);
    } else {
        FragColor = uColor;
    }
}
)";

unsigned Compile(GLenum type, const char* src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLFWmonitor* MonitorContaining(GLFWwindow* window) {
    if (!window) return glfwGetPrimaryMonitor();
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

int MonitorWidthOf(GLFWwindow* window) {
    GLFWmonitor* monitor = MonitorContaining(window);
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    return mode ? mode->width : 1920;
}

float BoardSidePx(const SSVEPBoardLayout& layout, int board, int monitorW) {
    if (board < 0 || board >= kSSVEPBoardCount || monitorW <= 0) return 8.0f;
    const float frac = std::clamp(layout.sizeFrac[board], 0.04f, 0.30f);
    return std::max(8.0f, static_cast<float>(monitorW) * frac);
}

constexpr float kIconGapPx = 12.0f;
const char* kKeyArtRel[5] = {
    "keys/shortkey.png",
    "keys/longkey.png",
    "keys/mouse_1.png",
    "keys/mouse_2.png",
    "keys/mouse_3.png"
};

int KeyArtIndex(ImGuiKey key) {
    switch (KeyIconCatalog::KindFor(key)) {
    case KeyIconCatalog::Kind::ShortKey: return 0;
    case KeyIconCatalog::Kind::LongKey: return 1;
    case KeyIconCatalog::Kind::MouseLeft: return 2;
    case KeyIconCatalog::Kind::MouseRight: return 3;
    case KeyIconCatalog::Kind::MouseMiddle: return 4;
    default: return -1;
    }
}

float TitleBlockPx(float side, float dpi) {
    return std::max(28.0f * dpi, side * 0.16f);
}
}

void CalibrationSession::Start(int board, const SSVEPBoardLayout& layout, GLFWwindow* owner, float dpi,
    unsigned patternTex, unsigned patternInvTex) {
    if (board < 0 || board >= kSSVEPBoardCount) return;
    Begin({ board }, layout, owner, dpi, patternTex, patternInvTex);
}

void CalibrationSession::StartAll(const SSVEPBoardLayout& layout, GLFWwindow* owner, float dpi,
    unsigned patternTex, unsigned patternInvTex) {
    int flicker[kSSVEPFlickerBoardCount];
    for (int i = 0; i < kSSVEPFlickerBoardCount; ++i) flicker[i] = i;
    std::sort(flicker, flicker + kSSVEPFlickerBoardCount, [&](int a, int b) {
        if (layout.customHz[a] != layout.customHz[b]) return layout.customHz[a] < layout.customHz[b];
        return a < b;
    });
    std::vector<int> order;
    order.push_back(kSSVEPNullBoard);
    for (int i = 0; i < kSSVEPFlickerBoardCount; ++i) order.push_back(flicker[i]);
    Begin(order, layout, owner, dpi, patternTex, patternInvTex);
}

void CalibrationSession::StartQueue(const std::vector<int>& boards, const SSVEPBoardLayout& layout,
    GLFWwindow* owner, float dpi, unsigned patternTex, unsigned patternInvTex) {
    if (boards.empty()) return;
    Begin(boards, layout, owner, dpi, patternTex, patternInvTex);
}

void CalibrationSession::Begin(const std::vector<int>& boards, const SSVEPBoardLayout& layout,
    GLFWwindow* owner, float dpi, unsigned patternTex, unsigned patternInvTex) {
    if (active_ || boards.empty() || !owner) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scratchSamples_.clear();
        scratchRate_ = 200;
    }
    queue_ = boards;
    queueIndex_ = 0;
    layout_ = layout;
    owner_ = owner;
    dpi_ = std::max(1.0f, dpi);
    patternTex_ = patternTex;
    patternInvTex_ = patternInvTex;
    countdown_ = true;
    pausing_ = false;
    recording_ = false;
    phaseStart_ = glfwGetTime();
    active_ = true;
    OpenWindow();
}

void CalibrationSession::Cancel() {
    if (!active_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scratchSamples_.clear();
        recording_ = false;
    }
    active_ = false;
    countdown_ = false;
    pausing_ = false;
    CloseWindow();
}

void CalibrationSession::Shutdown() {
    Cancel();
}

bool CalibrationSession::IsRecording() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recording_;
}

void CalibrationSession::PushSample(const float channels[4], int samplingRate) {
    if (!channels) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!recording_) return;
    if (samplingRate > 0) scratchRate_ = samplingRate;
    scratchSamples_.push_back({ channels[0], channels[1], channels[2], channels[3] });
}

const SSVEPCalibrationTrial& CalibrationSession::Trial(int board) const {
    static const SSVEPCalibrationTrial kEmpty;
    if (board < 0 || board >= kSSVEPBoardCount) return kEmpty;
    return stored_[board];
}

void CalibrationSession::CopyTrial(int board, SSVEPCalibrationTrial& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (board < 0 || board >= kSSVEPBoardCount) {
        out = {};
        return;
    }
    out = stored_[board];
}

bool CalibrationSession::HasTrial(int board) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (board < 0 || board >= kSSVEPBoardCount) return false;
    return stored_[board].complete && !stored_[board].samples.empty();
}

void CalibrationSession::ClearTrial(int board) {
    if (board < 0 || board >= kSSVEPFlickerBoardCount) return;
    std::lock_guard<std::mutex> lock(mutex_);
    stored_[board] = {};
    threshold_[board] = {};
}

void CalibrationSession::NoteCalibrationSnr(int board, float measuredSnr, float peakPower) {
    if (board < 0 || board >= kSSVEPFlickerBoardCount) return;
    if (!(peakPower > 0.0f)) return;
    measuredSnr = std::max(0.0f, measuredSnr);
    std::lock_guard<std::mutex> lock(mutex_);
    SSVEPBoardThreshold& slot = threshold_[board];
    const float automatic = 0.70f * measuredSnr;
    const float measuredScale = std::max(1.0f, measuredSnr);
    const float powerScale = std::max(1.0f, peakPower);
    const bool same = slot.valid
        && std::fabs(slot.measuredSnr - measuredSnr) <= 1.0e-4f * measuredScale
        && std::fabs(slot.peakPower - peakPower) <= 1.0e-4f * powerScale;
    if (same) return;
    slot.valid = true;
    slot.measuredSnr = measuredSnr;
    slot.autoSnr = automatic;
    slot.appliedSnr = automatic;
    slot.peakPower = peakPower;
    slot.lineScale = 0.70f;
}

void CalibrationSession::SetLineScale(int board, float lineScale) {
    if (board < 0 || board >= kSSVEPFlickerBoardCount) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!threshold_[board].valid) return;
    SSVEPBoardThreshold& slot = threshold_[board];
    slot.lineScale = std::max(0.0f, lineScale);
    slot.appliedSnr = slot.measuredSnr * slot.lineScale;
}

void CalibrationSession::ResetSnrThreshold(int board) {
    if (board < 0 || board >= kSSVEPFlickerBoardCount) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!threshold_[board].valid) return;
    threshold_[board].lineScale = 0.70f;
    threshold_[board].appliedSnr = threshold_[board].autoSnr;
}

bool CalibrationSession::CopySnrThreshold(int board, SSVEPBoardThreshold& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (board < 0 || board >= kSSVEPFlickerBoardCount || !threshold_[board].valid) {
        out = {};
        return false;
    }
    out = threshold_[board];
    return true;
}

void CalibrationSession::CommitCurrent() {
    if (queueIndex_ < 0 || queueIndex_ >= static_cast<int>(queue_.size())) return;
    const int board = queue_[queueIndex_];
    std::vector<std::array<float, 4>> samples;
    int rate = 200;
    float hz[4] = {};
    bool score = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stored_[board].samples = std::move(scratchSamples_);
        scratchSamples_.clear();
        stored_[board].samplingRate = scratchRate_ > 0 ? scratchRate_ : 200;
        stored_[board].complete = true;
        recording_ = false;
        if (!SSVEPBoardIsNull(board)) {
            samples = stored_[board].samples;
            rate = stored_[board].samplingRate;
            for (int i = 0; i < kSSVEPFlickerBoardCount; ++i) hz[i] = layout_.customHz[i];
            score = true;
        }
    }
    if (!score) return;

    int n = static_cast<int>(samples.size());
    if (n % 2) --n;
    if (n < 32) return;
    std::vector<float> column[4];
    const float* ptrs[4] = {};
    for (int ch = 0; ch < 4; ++ch) {
        column[ch].resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) column[ch][static_cast<size_t>(i)] = samples[static_cast<size_t>(i)][ch];
        ptrs[ch] = column[ch].data();
    }
    float snr[4] = {};
    if (!MeasureHarmonicSnr(ptrs, n, rate, hz, snr)) return;
    const float peak = HannPeakPower(ptrs, n, rate, hz[board]);
    NoteCalibrationSnr(board, snr[board], peak);
}

void CalibrationSession::Advance(double nowSec) {
    ++queueIndex_;
    if (queueIndex_ >= static_cast<int>(queue_.size())) {
        active_ = false;
        countdown_ = false;
        CloseWindow();
        return;
    }
    countdown_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scratchSamples_.clear();
        recording_ = true;
    }
    phaseStart_ = nowSec;
}

void CalibrationSession::SetRecordSeconds(float sec) {
    if (sec < 3.0f) sec = 3.0f;
    if (sec > 7.0f) sec = 7.0f;
    recordSec_ = sec;
}

void CalibrationSession::Update(double nowSec) {
    if (!active_) return;
    if (countdown_) {
        if (nowSec - phaseStart_ >= kCountdownSec) {
            countdown_ = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                scratchSamples_.clear();
                recording_ = true;
            }
            phaseStart_ = nowSec;
        }
        return;
    }
    if (pausing_) {
        if (nowSec - phaseStart_ >= kPauseSec) {
            pausing_ = false;
            Advance(nowSec);
        }
        return;
    }
    if (nowSec - phaseStart_ >= static_cast<double>(recordSec_)) {
        CommitCurrent();
        if (queueIndex_ + 1 >= static_cast<int>(queue_.size())) {
            Advance(nowSec);
            return;
        }
        pausing_ = true;
        phaseStart_ = nowSec;
    }
}

void CalibrationSession::OpenWindow() {
    if (window_ || !owner_) return;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
#ifdef GLFW_MOUSE_PASSTHROUGH
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
#endif

    const int monitorW = MonitorWidthOf(owner_);
    int artW[5] = {};
    int artH[5] = {};
    for (int i = 0; i < 5; ++i) {
        int comp = 0;
        stbi_info(GetAssetPath(kKeyArtRel[i]).c_str(), &artW[i], &artH[i], &comp);
    }
    const float longW = artW[1] > 0 ? static_cast<float>(artW[1]) : 388.0f;
    const float longH = artH[1] > 0 ? static_cast<float>(artH[1]) : 244.0f;
    float needW = 8.0f;
    float above = 0.0f;
    float below = 0.0f;
    for (int board : queue_) {
        const float side = BoardSidePx(layout_, board, monitorW);
        const float titleH = TitleBlockPx(side, dpi_);
        const float titleGap = 10.0f * dpi_;
        above = std::max(above, side * 0.5f + titleGap + titleH);
        float stackBelow = side * 0.5f;
        if (!SSVEPBoardIsNull(board)) {
            const int idx = KeyArtIndex(InputManager::GetInstance().GetKey(board));
            if (idx >= 0 && artW[idx] > 0 && artH[idx] > 0) {
                float dw = 0.0f, dh = 0.0f;
                KeyIconCatalog::DisplaySize(static_cast<float>(artW[idx]), static_cast<float>(artH[idx]),
                    side, longW, longH, dw, dh);
                stackBelow = side * 0.5f + kIconGapPx + dh;
                needW = std::max(needW, dw);
            }
        }
        needW = std::max(needW, side);
        below = std::max(below, stackBelow);
    }
    const float margin = 24.0f * dpi_;
    const float cancelBlock = (40.0f + 28.0f + 12.0f) * dpi_;
    const float winWf = std::max(640.0f * dpi_, needW + margin * 2.0f);
    float winHf = std::max(760.0f * dpi_, (above + margin) / 0.42f);
    winHf = std::max(winHf, (below + margin + cancelBlock) / 0.58f);
    const int winW = static_cast<int>(std::lround(winWf));
    const int winH = static_cast<int>(std::lround(winHf));
    window_ = glfwCreateWindow(winW, winH, "SSVEP Calibration", nullptr, owner_);

    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    if (!window_) return;

    GLFWmonitor* monitor = MonitorContaining(owner_);
    int mx = 0, my = 0;
    if (monitor) glfwGetMonitorPos(monitor, &mx, &my);
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    const int mw = mode ? mode->width : winW;
    const int mh = mode ? mode->height : winH;
    glfwSetWindowPos(window_, mx + (mw - winW) / 2, my + (mh - winH) / 2);
    glfwSetWindowAttrib(window_, GLFW_FLOATING, GLFW_TRUE);
#ifdef GLFW_MOUSE_PASSTHROUGH
    glfwSetWindowAttrib(window_, GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
#endif
#if defined(_WIN32)
    if (HWND hwnd = glfwGetWin32Window(window_)) {
        LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif

    GLFWwindow* previous = glfwGetCurrentContext();
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(0);

    unsigned vs = Compile(GL_VERTEX_SHADER, kVert);
    unsigned fs = Compile(GL_FRAGMENT_SHADER, kFrag);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    const std::string fontPath = GetAssetPath("Nunito-ExtraBold.ttf");
    std::ifstream fontFile(fontPath, std::ios::binary);
    if (fontFile) {
        std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(fontFile)),
            std::istreambuf_iterator<char>());
        auto* baked = new stbtt_bakedchar[96];
        std::vector<unsigned char> atlas(static_cast<size_t>(fontAtlas_) * fontAtlas_, 0);
        const int bake = stbtt_BakeFontBitmap(ttf.data(), 0, 96.0f, atlas.data(), fontAtlas_, fontAtlas_, 32, 96, baked);
        if (bake > 0) {
#if defined(_WIN32)
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
                        const float arrowScale = stbtt_ScaleForPixelHeight(&arrowInfo, 96.0f);
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
                            if (gw <= 0 || gh <= 0 || penY + gh >= fontAtlas_) {
                                arrowReady_[i] = false;
                                continue;
                            }
                            if (penX + gw + 1 >= fontAtlas_) {
                                penX = 1;
                                penY += rowH + 1;
                                rowH = 0;
                                if (penY + gh >= fontAtlas_) {
                                    arrowReady_[i] = false;
                                    continue;
                                }
                            }
                            stbtt_MakeCodepointBitmap(&arrowInfo,
                                atlas.data() + static_cast<size_t>(penY * fontAtlas_ + penX),
                                gw, gh, fontAtlas_, arrowScale, arrowScale, cp);
                            arrows[i].x0 = static_cast<unsigned short>(penX);
                            arrows[i].y0 = static_cast<unsigned short>(penY);
                            arrows[i].x1 = static_cast<unsigned short>(penX + gw);
                            arrows[i].y1 = static_cast<unsigned short>(penY + gh);
                            arrows[i].xoff = static_cast<float>(x0);
                            arrows[i].yoff = static_cast<float>(y0);
                            arrows[i].xadvance = arrowScale * static_cast<float>(advance);
                            penX += gw + 2;
                            if (gh > rowH) rowH = gh;
                            arrowReady_[i] = true;
                            any = true;
                        }
                        if (any) arrowChars_ = arrows;
                        else delete[] arrows;
                    }
                }
            }
#endif
            glGenTextures(1, &fontTex_);
            glBindTexture(GL_TEXTURE_2D, fontTex_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fontAtlas_, fontAtlas_, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            fontChars_ = baked;
        }
        else {
            delete[] baked;
        }

        float xScale = 1.0f, yScale = 1.0f;
        glfwGetWindowContentScale(window_, &xScale, &yScale);
        int fbW = 0, fbH = 0, boxW = 0, boxH = 0;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        glfwGetWindowSize(window_, &boxW, &boxH);
        const float fbPerWindow = (boxW > 0 && fbW > 0)
            ? static_cast<float>(fbW) / static_cast<float>(boxW)
            : std::max(xScale, yScale);
        const float digitPx = std::max(128.0f, 180.0f * dpi_ * fbPerWindow);
        auto* digits = new stbtt_bakedchar[3];
        int digitSide = 512;
        while (digitSide < static_cast<int>(std::ceil(digitPx * 2.0f)) && digitSide < 4096) digitSide *= 2;
        bool digitOk = false;
        for (int attempt = 0; attempt < 3 && !digitOk; ++attempt) {
            std::vector<unsigned char> pixels(static_cast<size_t>(digitSide) * digitSide, 0);
            const int digitBake = stbtt_BakeFontBitmap(
                ttf.data(), 0, digitPx, pixels.data(), digitSide, digitSide, '1', 3, digits);
            if (digitBake > 0) {
                glGenTextures(1, &digitTex_);
                glBindTexture(GL_TEXTURE_2D, digitTex_);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, digitSide, digitSide, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                digitAtlas_ = digitSide;
                digitChars_ = digits;
                digitOk = true;
            }
            else if (digitSide < 4096) {
                digitSide = std::min(4096, digitSide * 2);
            }
            else {
                break;
            }
        }
        if (!digitOk) delete[] digits;
    }

    for (int i = 0; i < 5; ++i) {
        keyTex_[i] = LoadComponentTexture(GetAssetPath(kKeyArtRel[i]).c_str(), &keyW_[i], &keyH_[i]);
    }
    glBindVertexArray(0);
    if (previous) glfwMakeContextCurrent(previous);
}

void CalibrationSession::CloseWindow() {
    if (!window_) return;
    GLFWwindow* previous = glfwGetCurrentContext();
    glfwMakeContextCurrent(window_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
    if (fontTex_) glDeleteTextures(1, &fontTex_);
    if (digitTex_) glDeleteTextures(1, &digitTex_);
    for (unsigned& tex : keyTex_) {
        if (tex) glDeleteTextures(1, &tex);
        tex = 0;
    }
    if (fontChars_) delete[] static_cast<stbtt_bakedchar*>(fontChars_);
    if (digitChars_) delete[] static_cast<stbtt_bakedchar*>(digitChars_);
    if (arrowChars_) delete[] static_cast<stbtt_bakedchar*>(arrowChars_);
    vao_ = vbo_ = program_ = fontTex_ = digitTex_ = 0;
    fontChars_ = digitChars_ = arrowChars_ = nullptr;
    digitAtlas_ = 0;
    arrowReady_[0] = arrowReady_[1] = arrowReady_[2] = arrowReady_[3] = false;
    keyW_[0] = keyW_[1] = keyW_[2] = keyW_[3] = keyW_[4] = 0;
    keyH_[0] = keyH_[1] = keyH_[2] = keyH_[3] = keyH_[4] = 0;
    glfwDestroyWindow(window_);
    window_ = nullptr;
    if (previous && previous != window_) glfwMakeContextCurrent(previous);
}

void CalibrationSession::DrawBoard(float cx, float cy, float side) {
    const int board = queue_[queueIndex_];
    const bool nullBoard = SSVEPBoardIsNull(board);
    const float x0 = cx - side * 0.5f;
    const float y0 = cy - side * 0.5f;
    const float x1 = cx + side * 0.5f;
    const float y1 = cy + side * 0.5f;

    auto quad = [&](float ax, float ay, float bx, float by, float r, float g, float b, float a, unsigned tex, int mode) {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) return;
        int boxW = 0, boxH = 0;
        glfwGetWindowSize(window_, &boxW, &boxH);
        if (boxW <= 0 || boxH <= 0) return;
        auto X = [&](float x) { return x / static_cast<float>(boxW) * 2.0f - 1.0f; };
        auto Y = [&](float y) { return 1.0f - y / static_cast<float>(boxH) * 2.0f; };
        const float v[] = {
            X(ax), Y(ay), 0.0f, 0.0f,
            X(ax), Y(by), 0.0f, 1.0f,
            X(bx), Y(ay), 1.0f, 0.0f,
            X(bx), Y(by), 1.0f, 1.0f
        };
        glUseProgram(program_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glUniform1i(glGetUniformLocation(program_, "uMode"), mode);
        glUniform4f(glGetUniformLocation(program_, "uColor"), r, g, b, a);
        if (tex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    float alpha = 1.0f;
    bool contrast = false;
    if (!nullBoard && !pausing_) {
        contrast = SSVEPPatternHasContrast(layout_.patternMode) && layout_.contrastMode;
        const double target = contrast ? (static_cast<double>(layout_.customHz[board]) / 2.0) : static_cast<double>(layout_.customHz[board]);
        const double phase = std::fmod(glfwGetTime() * target, 1.0);
        const bool on = phase < 0.5;
        alpha = on ? 1.0f : 0.0f;
        if (layout_.smoothMode) {
            alpha = 0.5f - 0.5f * static_cast<float>(std::cos(2.0 * 3.14159265358979 * phase));
        }
    }

    if (contrast && patternTex_) {
        quad(x0, y0, x1, y1, alpha, alpha, alpha, 1.0f, patternTex_, 4);
    }
    else if (patternTex_) {
        quad(x0, y0, x1, y1, alpha, alpha, alpha, 1.0f, patternTex_, 2);
    }
    else {
        quad(x0, y0, x1, y1, alpha, alpha, alpha, 1.0f, 0, 0);
    }
    if (nullBoard) {
        const float t = std::max(2.0f, 3.0f * dpi_);
        const float r = 184.0f / 255.0f, g = 134.0f / 255.0f, b = 11.0f / 255.0f;
        quad(x0, y0, x1, y0 + t, r, g, b, 1.0f, 0, 0);
        quad(x0, y1 - t, x1, y1, r, g, b, 1.0f, 0, 0);
        quad(x0, y0, x0 + t, y1, r, g, b, 1.0f, 0, 0);
        quad(x1 - t, y0, x1, y1, r, g, b, 1.0f, 0, 0);
    }

    const float fixD = side * kSSVEPFixationDiameterFrac;
    quad(cx - fixD * 0.5f, cy - fixD * 0.5f, cx + fixD * 0.5f, cy + fixD * 0.5f,
        alpha, alpha, alpha, 1.0f, 0, 6);

    char title[64];
    if (nullBoard) snprintf(title, sizeof(title), "Null");
    else snprintf(title, sizeof(title), "%.1f Hz", layout_.customHz[board]);
    const float titleH = TitleBlockPx(side, dpi_);
    const float titleGap = 10.0f * dpi_;
    const float titleCy = y0 - titleGap - titleH * 0.5f;
    const float titleW = std::max(side, titleH * 6.0f);
    if (nullBoard) DrawLabel(title, cx, titleCy, titleW, titleH, 0.72f, 0.53f, 0.04f);
    else DrawLabel(title, cx, titleCy, titleW, titleH, 0.12f, 0.12f, 0.12f);

    if (!nullBoard) {
        const ImGuiKey key = InputManager::GetInstance().GetKey(board);
        const int idx = KeyArtIndex(key);
        if (idx >= 0 && keyTex_[idx] && keyW_[idx] > 0 && keyH_[idx] > 0) {
            const float longW = keyW_[1] > 0 ? static_cast<float>(keyW_[1]) : static_cast<float>(keyW_[idx]);
            const float longH = keyH_[1] > 0 ? static_cast<float>(keyH_[1]) : static_cast<float>(keyH_[idx]);
            float dw = 0.0f, dh = 0.0f;
            KeyIconCatalog::DisplaySize(static_cast<float>(keyW_[idx]), static_cast<float>(keyH_[idx]),
                side, longW, longH, dw, dh);
            const float dx = cx - dw * 0.5f;
            const float dy = y1 + kIconGapPx;
            quad(dx, dy, dx + dw, dy + dh, 1.0f, 1.0f, 1.0f, 1.0f, keyTex_[idx], 2);

            const std::string caption = KeyIconCatalog::Caption(key);
            if (!caption.empty()) {
                const bool longKey = KeyIconCatalog::KindFor(key) == KeyIconCatalog::Kind::LongKey;
                DrawLabel(caption.c_str(), dx + dw * 0.5f, dy + dh * 0.38f,
                    dw * (longKey ? 0.62f : 0.50f),
                    dh * (longKey ? 0.22f : 0.34f),
                    0.10f, 0.10f, 0.10f);
            }
        }
    }
}

void CalibrationSession::DrawLabel(const char* text, float cx, float cy, float maxW, float maxH, float r, float g, float b) {
    if (!text || !text[0] || !fontChars_ || !fontTex_ || !window_) return;
    auto* baked = static_cast<stbtt_bakedchar*>(fontChars_);
    auto* arrows = static_cast<stbtt_bakedchar*>(arrowChars_);

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
        if (arrows && cp >= 0x2190 && cp <= 0x2193 && arrowReady_[cp - 0x2190]) {
            out = arrows[cp - 0x2190];
            return true;
        }
        return false;
    };

    float x = 0.0f, y = 0.0f;
    float y0 = 1.0e9f, y1 = -1.0e9f;
    for (const char* p = text; *p; ) {
        const int cp = nextCp(p);
        stbtt_bakedchar glyph;
        if (cp <= 0 || !glyphFor(cp, glyph)) continue;
        stbtt_aligned_quad q;
        float qx = x, qy = y;
        stbtt_GetBakedQuad(&glyph, fontAtlas_, fontAtlas_, 0, &qx, &qy, &q, 1);
        x += glyph.xadvance;
        y0 = std::min(y0, q.y0);
        y1 = std::max(y1, q.y1);
    }
    if (x <= 1.0f || y1 <= y0) return;

    const float scale = std::min(maxW / x, maxH / (y1 - y0));
    const float originX = cx - 0.5f * x * scale;
    const float originY = cy - 0.5f * (y0 + y1) * scale;

    int boxW = 0, boxH = 0;
    glfwGetWindowSize(window_, &boxW, &boxH);
    if (boxW <= 0 || boxH <= 0) return;
    auto X = [&](float px) { return px / static_cast<float>(boxW) * 2.0f - 1.0f; };
    auto Y = [&](float py) { return 1.0f - py / static_cast<float>(boxH) * 2.0f; };

    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glUniform1i(glGetUniformLocation(program_, "uMode"), 1);
    glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
    glUniform4f(glGetUniformLocation(program_, "uColor"), r, g, b, 1.0f);

    x = 0.0f;
    y = 0.0f;
    for (const char* p = text; *p; ) {
        const int cp = nextCp(p);
        stbtt_bakedchar glyph;
        if (cp <= 0 || !glyphFor(cp, glyph)) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(&glyph, fontAtlas_, fontAtlas_, 0, &x, &y, &q, 1);
        const float gx0 = originX + q.x0 * scale;
        const float gy0 = originY + q.y0 * scale;
        const float gx1 = originX + q.x1 * scale;
        const float gy1 = originY + q.y1 * scale;
        const float tv[] = {
            X(gx0), Y(gy0), q.s0, q.t0,
            X(gx0), Y(gy1), q.s0, q.t1,
            X(gx1), Y(gy0), q.s1, q.t0,
            X(gx1), Y(gy1), q.s1, q.t1
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(tv), tv, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void CalibrationSession::DrawCancel(int winW, int winH) {
    const float bw = 160.0f * dpi_;
    const float bh = 40.0f * dpi_;
    const float x0 = (static_cast<float>(winW) - bw) * 0.5f;
    const float y0 = static_cast<float>(winH) - bh - 28.0f * dpi_;
    if (winW <= 0 || winH <= 0) return;
    auto X = [&](float x) { return x / static_cast<float>(winW) * 2.0f - 1.0f; };
    auto Y = [&](float y) { return 1.0f - y / static_cast<float>(winH) * 2.0f; };
    const float v[] = {
        X(x0), Y(y0), 0, 0,
        X(x0), Y(y0 + bh), 0, 1,
        X(x0 + bw), Y(y0), 1, 0,
        X(x0 + bw), Y(y0 + bh), 1, 1
    };
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glUniform1i(glGetUniformLocation(program_, "uMode"), 0);
    glUniform4f(glGetUniformLocation(program_, "uColor"), 0.85f, 0.12f, 0.12f, 1.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (!fontChars_ || !fontTex_) return;
    auto* baked = static_cast<stbtt_bakedchar*>(fontChars_);
    const char* text = "Cancel";
    float x = 0.0f, y = 0.0f, y0g = 1e9f, y1g = -1e9f;
    for (const char* p = text; *p; ++p) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(baked, fontAtlas_, fontAtlas_, static_cast<unsigned char>(*p) - 32, &x, &y, &q, 1);
        y0g = std::min(y0g, q.y0);
        y1g = std::max(y1g, q.y1);
    }
    const float scale = std::min((bw * 0.7f) / std::max(1.0f, x), (bh * 0.55f) / std::max(1.0f, y1g - y0g));
    const float originX = x0 + bw * 0.5f - x * scale * 0.5f;
    const float originY = y0 + bh * 0.5f - (y0g + y1g) * 0.5f * scale;
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glUniform1i(glGetUniformLocation(program_, "uMode"), 1);
    glUniform4f(glGetUniformLocation(program_, "uColor"), 1, 1, 1, 1);
    x = 0; y = 0;
    for (const char* p = text; *p; ++p) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(baked, fontAtlas_, fontAtlas_, static_cast<unsigned char>(*p) - 32, &x, &y, &q, 1);
        const float gx0 = originX + q.x0 * scale;
        const float gy0 = originY + q.y0 * scale;
        const float gx1 = originX + q.x1 * scale;
        const float gy1 = originY + q.y1 * scale;
        const float tv[] = {
            X(gx0), Y(gy0), q.s0, q.t0,
            X(gx0), Y(gy1), q.s0, q.t1,
            X(gx1), Y(gy0), q.s1, q.t0,
            X(gx1), Y(gy1), q.s1, q.t1
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(tv), tv, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

bool CalibrationSession::CancelHit(double mx, double my, int winW, int winH) const {
    const float bw = 160.0f * dpi_;
    const float bh = 40.0f * dpi_;
    const float x0 = (static_cast<float>(winW) - bw) * 0.5f;
    const float y0 = static_cast<float>(winH) - bh - 28.0f * dpi_;
    return mx >= x0 && mx <= x0 + bw && my >= y0 && my <= y0 + bh;
}

void CalibrationSession::Render() {
    if (!active_ || !window_) return;
#if defined(_WIN32)
    if (HWND hwnd = glfwGetWin32Window(window_)) {
        LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (ex & WS_EX_TRANSPARENT) {
            ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        else {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
#endif
    const bool esc = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS
        || (owner_ && glfwGetKey(owner_, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    int ww = 0, wh = 0;
    glfwGetWindowSize(window_, &ww, &wh);
    bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    double mx = 0, my = 0;
    glfwGetCursorPos(window_, &mx, &my);
#if defined(_WIN32)
    if (HWND hwnd = glfwGetWin32Window(window_)) {
        POINT cursor;
        if (GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor)) {
            mx = cursor.x;
            my = cursor.y;
        }
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) down = true;
    }
#endif
    if ((down && !mouseDown_ && CancelHit(mx, my, ww, wh)) || (esc && !escapeDown_)) {
        mouseDown_ = down;
        escapeDown_ = esc;
        Cancel();
        return;
    }
    mouseDown_ = down;
    escapeDown_ = esc;

    GLFWwindow* previous = glfwGetCurrentContext();
    glfwMakeContextCurrent(window_);
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (countdown_) {
        const double elapsed = glfwGetTime() - phaseStart_;
        const int digit = elapsed < 1.0 ? 3 : (elapsed < 2.0 ? 2 : 1);
        const bool sharpDigits = digitChars_ && digitTex_ && digitAtlas_ > 0;
        if (sharpDigits || (fontChars_ && fontTex_)) {
            auto* baked = static_cast<stbtt_bakedchar*>(sharpDigits ? digitChars_ : fontChars_);
            const int atlas = sharpDigits ? digitAtlas_ : fontAtlas_;
            const int glyph = sharpDigits ? (digit - 1) : (digit + 16);
            float x = 0, y = 0, y0 = 1e9f, y1 = -1e9f;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(baked, atlas, atlas, glyph, &x, &y, &q, 1);
            y0 = q.y0; y1 = q.y1;
            const float scale = sharpDigits
                ? (static_cast<float>(ww) / static_cast<float>(std::max(1, fbW)))
                : ((180.0f * dpi_) / std::max(1.0f, y1 - y0));
            const float ox = static_cast<float>(ww) * 0.5f - x * scale * 0.5f;
            const float oy = static_cast<float>(wh) * 0.42f - (y0 + y1) * 0.5f * scale;
            auto X = [&](float px) { return px / static_cast<float>(ww) * 2.0f - 1.0f; };
            auto Y = [&](float py) { return 1.0f - py / static_cast<float>(wh) * 2.0f; };
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glBindTexture(GL_TEXTURE_2D, sharpDigits ? digitTex_ : fontTex_);
            glUniform1i(glGetUniformLocation(program_, "uMode"), 1);
            glUniform4f(glGetUniformLocation(program_, "uColor"), 0.1f, 0.1f, 0.1f, 1);
            x = 0; y = 0;
            stbtt_GetBakedQuad(baked, atlas, atlas, glyph, &x, &y, &q, 1);
            const float gx0 = ox + q.x0 * scale;
            const float gy0 = oy + q.y0 * scale;
            const float gx1 = ox + q.x1 * scale;
            const float gy1 = oy + q.y1 * scale;
            const float tv[] = {
                X(gx0), Y(gy0), q.s0, q.t0,
                X(gx0), Y(gy1), q.s0, q.t1,
                X(gx1), Y(gy0), q.s1, q.t0,
                X(gx1), Y(gy1), q.s1, q.t1
            };
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(tv), tv, GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
    else if (queueIndex_ >= 0 && queueIndex_ < static_cast<int>(queue_.size())) {
        const int board = queue_[queueIndex_];
        const float side = BoardSidePx(layout_, board, MonitorWidthOf(owner_));
        DrawBoard(static_cast<float>(ww) * 0.5f, static_cast<float>(wh) * 0.42f, side);
    }
    DrawCancel(ww, wh);
    glfwSwapBuffers(window_);
    if (previous) glfwMakeContextCurrent(previous);
}
