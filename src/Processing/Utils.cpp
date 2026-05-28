#include "Utils.h"
#include <iostream>
#include <glad/glad.h>
#include "stb_image.h"

unsigned int LoadComponentTexture(const char* filepath) {
    int width, height, channels;
    // Force stbi to load 4 channels (RGBA) for clean rendering
    unsigned char* data = stbi_load(filepath, &width, &height, &channels, 4);

    if (!data) {
        std::cout << "Failed to load image texture: " << filepath << std::endl;
        return 0;
    }

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