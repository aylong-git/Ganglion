#pragma once
#include <string>

// Resolve a file under AppCore/assets without depending on the process CWD.
// Example: GetAssetPath("head_object.obj")
std::string GetAssetPath(const std::string& relativeUnderAssets);

unsigned int LoadComponentTexture(const char* filepath, int* outW = nullptr, int* outH = nullptr);
