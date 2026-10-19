#include "Utils.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <glad/glad.h>
#include "stb_image.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

static fs::path ExecutableDirectory() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return fs::current_path();
    return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return fs::current_path();
    return fs::weakly_canonical(fs::path(buf.c_str())).parent_path();
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return fs::current_path();
    return p.parent_path();
#endif
}

std::string GetAssetPath(const std::string& relativeUnderAssets) {
    const fs::path rel(relativeUnderAssets);
    std::vector<fs::path> roots;
    const fs::path exeDir = ExecutableDirectory();
    roots.push_back(exeDir / "assets");
#ifdef GANGLION_ASSET_DIR
    roots.push_back(fs::path(GANGLION_ASSET_DIR));
#endif
    roots.push_back(fs::current_path() / "AppCore" / "assets");
    roots.push_back(fs::current_path() / "assets");

    for (const fs::path& root : roots) {
        fs::path candidate = root / rel;
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            return candidate.string();
        }
    }
    // Last resort: next to the exe, so logs show a stable expected location.
    return (exeDir / "assets" / rel).string();
}

unsigned int LoadComponentTexture(const char* filepath, int* outW, int* outH) {
    int width, height, channels;
    // Force stbi to load 4 channels (RGBA) for clean rendering
    unsigned char* data = stbi_load(filepath, &width, &height, &channels, 4);

    if (!data) {
        std::cout << "Failed to load image texture: " << filepath << std::endl;
        return 0;
    }

    if (outW) *outW = width;
    if (outH) *outH = height;

    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Upload raw pixel data to GPU memory
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Set texture filtering properties
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data); // Free the RAM copy since it lives safely on the GPU now
    return textureID;
}