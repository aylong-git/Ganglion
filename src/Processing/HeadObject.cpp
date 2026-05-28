#include <glad/glad.h>
#include "HeadObject.h"
#include <iostream>
#include <cmath>
#include <algorithm>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ==========================================================
// AUTO-SCALING SHADERS (No more clipping or drifting!)
// ==========================================================
const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTexCoord;
    layout (location = 3) in vec3 aColor; 
    layout (location = 4) in float aObjectID; // FIXED: Declare the incoming Object ID!
    
    uniform vec2 uRotation;

    out vec2 TexCoord;
    out vec3 Normal;
    out vec3 VertexColor; 
    out float ObjectID; // FIXED: Declare the outgoing Object ID to the fragment shader!
    
    void main() {
        float cx = cos(radians(uRotation.x)), sx = sin(radians(uRotation.x));
        float cy = cos(radians(uRotation.y)), sy = sin(radians(uRotation.y));

        vec3 p = aPos;
        float x1 = p.x * cy - p.z * sy; float z1 = p.x * sy + p.z * cy;
        p.x = x1; p.z = z1;
        float y2 = p.y * cx - p.z * sx; float z2 = p.y * sx + p.z * cx;
        p.y = y2; p.z = z2;

        vec3 n = aNormal;
        float nx1 = n.x * cy - n.z * sy; float nz1 = n.x * sy + n.z * cy;
        n.x = nx1; n.z = nz1;
        float ny2 = n.y * cx - n.z * sx; float nz2 = n.y * sx + n.z * cx;
        n.y = ny2; n.z = nz2;

        gl_Position = vec4(p.x, p.y, p.z * 0.1, 1.0);
        TexCoord = aTexCoord; 
        Normal = n; 
        VertexColor = aColor;
        
        ObjectID = aObjectID; // Now the compiler knows what these are!
    }
)";

const char* fragmentShaderSource = R"(
    #version 330 core
    in vec2 TexCoord;
    in vec3 Normal;
    in vec3 VertexColor; 
    in float ObjectID; // FIXED: Receive the Object ID from the vertex shader!
    
    out vec4 FragColor;
    
    uniform sampler2D texture1; 
    uniform float uSelectedObjectID; // FIXED: Declare the uniform from C++!
    
    void main() {
        vec3 norm = normalize(Normal);
        vec3 keyLightDir = normalize(vec3(0.8, 0.8, 1.0));
        float keyDiff = max(dot(norm, keyLightDir), 0.0);
        vec3 fillLightDir = normalize(vec3(-0.8, -0.3, 0.5));
        float fillDiff = max(dot(norm, fillLightDir), 0.0) * 0.3; 
        float ambient = 0.35;
        
        float totalLighting = ambient + (keyDiff * 0.6) + fillDiff;
        vec4 texColor = texture(texture1, TexCoord);
        vec3 finalColor = VertexColor * texColor.rgb * totalLighting;

        // VISUAL FEEDBACK
        if (abs(ObjectID - uSelectedObjectID) < 0.1) {
            finalColor = mix(finalColor, vec3(0.0, 0.4, 1.0), 0.4); 
        }
        
        FragColor = vec4(finalColor, 1.0);
    }
)";

HeadObject::~HeadObject() {
    if (my3DHead.vboID) glDeleteBuffers(1, &my3DHead.vboID);
    if (my3DHead.vaoID) glDeleteVertexArrays(1, &my3DHead.vaoID);
    if (my3DHead.fboID) glDeleteFramebuffers(1, &my3DHead.fboID);
    if (my3DHead.renderTextureID) glDeleteTextures(1, &my3DHead.renderTextureID);
    if (my3DHead.modelTextureID) glDeleteTextures(1, &my3DHead.modelTextureID);
    if (my3DHead.rboID) glDeleteRenderbuffers(1, &my3DHead.rboID);
    if (my3DHead.shaderProgram) glDeleteProgram(my3DHead.shaderProgram);
}

void HeadObject::SetupShaders() {
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    my3DHead.shaderProgram = glCreateProgram();
    glAttachShader(my3DHead.shaderProgram, vertexShader);
    glAttachShader(my3DHead.shaderProgram, fragmentShader);
    glLinkProgram(my3DHead.shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

void HeadObject::InitModel(const std::string& objFilePath, const std::string& mtlSearchPath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objFilePath.c_str(), mtlSearchPath.c_str());
    if (!ret) return;

    // ==========================================================
    // PASS 1: Calculate precise geometry bounds of the file
    // ==========================================================
    float minX = 1e10f, maxX = -1e10f, minY = 1e10f, maxY = -1e10f, minZ = 1e10f, maxZ = -1e10f;
    for (size_t s = 0; s < shapes.size(); s++) {
        for (size_t i = 0; i < shapes[s].mesh.indices.size(); i++) {
            tinyobj::index_t idx = shapes[s].mesh.indices[i];
            float vx = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
            float vy = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
            float vz = attrib.vertices[3 * size_t(idx.vertex_index) + 2];
            minX = std::min(minX, vx); maxX = std::max(maxX, vx);
            minY = std::min(minY, vy); maxY = std::max(maxY, vy);
            minZ = std::min(minZ, vz); maxZ = std::max(maxZ, vz);
        }
    }

    // Determine the exact center and a universal scaling multiplier
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float cz = (minZ + maxZ) * 0.5f;
    float maxDim = std::max({ maxX - minX, maxY - minY, maxZ - minZ });
    if (maxDim == 0.0f) maxDim = 1.0f;
    float scale = 1.4f / maxDim; // Squishes ANY massive mesh down to fit inside the camera view
    // ==========================================================
    // PASS 2: Apply Math, Normals, and PER-VERTEX COLORS
    // ==========================================================
    subObjects.clear();

    for (size_t s = 0; s < shapes.size(); s++) {
        SubObject obj;
        obj.id = static_cast<int>(s) + 1; // ID 1, 2, 3...
        obj.name = shapes[s].name;
        if (obj.name != "head_object") {

            obj.isSelectable = true;
            obj.description = "This is the " + obj.name + " component.";
        }
        else {
            obj.isSelectable = false;
            obj.description = ""; // We don't need a description for background objects
        }

        float sumX = 0, sumY = 0, sumZ = 0;
        int vertCount = 0;

        size_t index_offset = 0;
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
            int fv = shapes[s].mesh.num_face_vertices[f];
            int mat_id = shapes[s].mesh.material_ids[f];

            float r = 1.0f, g = 1.0f, b = 1.0f;
            if (mat_id >= 0 && mat_id < materials.size()) {
                r = materials[mat_id].diffuse[0];
                g = materials[mat_id].diffuse[1];
                b = materials[mat_id].diffuse[2];
            }

            for (size_t v = 0; v < fv; v++) {
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                float vx = (attrib.vertices[3 * size_t(idx.vertex_index) + 0] - cx) * scale;
                float vy = (attrib.vertices[3 * size_t(idx.vertex_index) + 1] - cy) * scale;
                float vz = (attrib.vertices[3 * size_t(idx.vertex_index) + 2] - cz) * scale;

                // Accumulate values to find the mathematical center of this specific object
                sumX += vx; sumY += vy; sumZ += vz;
                vertCount++;

                // Push values to VBO (Now 12 floats total per vertex)
                my3DHead.vertices.push_back(vx);
                my3DHead.vertices.push_back(vy);
                my3DHead.vertices.push_back(vz);

                // Normal (3)
                float nx = 0.0f, ny = 0.0f, nz = 1.0f;
                if (idx.normal_index >= 0) {
                    nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
                    ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
                    nz = attrib.normals[3 * size_t(idx.normal_index) + 2];
                }
                my3DHead.vertices.push_back(nx);
                my3DHead.vertices.push_back(ny);
                my3DHead.vertices.push_back(nz);

                // UV (2)
                float tx = 0.0f, ty = 0.0f;
                if (idx.texcoord_index >= 0) {
                    tx = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
                    ty = 1.0f - attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
                }
                my3DHead.vertices.push_back(tx);
                my3DHead.vertices.push_back(ty);

                // Color (3) - NEW! Push the material color into the GPU data
                my3DHead.vertices.push_back(r);
                my3DHead.vertices.push_back(g);
                my3DHead.vertices.push_back(b);

                my3DHead.vertices.push_back(static_cast<float>(obj.id));
            }
            index_offset += fv;
        }

        if (vertCount > 0) {
            obj.centerX = sumX / vertCount;
            obj.centerY = sumY / vertCount;
            obj.centerZ = sumZ / vertCount;
        }
        subObjects.push_back(obj);
    }

    // ==========================================================
    // TEXTURE FALLBACK (Now much simpler!)
    // ==========================================================
    glGenTextures(1, &my3DHead.modelTextureID);
    glBindTexture(GL_TEXTURE_2D, my3DHead.modelTextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    bool textureLoaded = false;
    if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
        std::string texturePath = mtlSearchPath + "/" + materials[0].diffuse_texname;
        int width, height, nrChannels;
        unsigned char* data = stbi_load(texturePath.c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);
            stbi_image_free(data);
            textureLoaded = true;
        }
    }

    // If no texture file is found, we just upload a pure white pixel.
    // The Fragment Shader multiplies this white pixel by the Per-Vertex Color!
    if (!textureLoaded) {
        unsigned char defaultWhite[3] = { 255, 255, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, defaultWhite);
    }

    SetupShaders();

    // ==========================================================
    // VBO / VAO SETUP (Now updated for 12 floats per vertex!)
    // ==========================================================
    glGenVertexArrays(1, &my3DHead.vaoID);
    glGenBuffers(1, &my3DHead.vboID);

    glBindVertexArray(my3DHead.vaoID);
    glBindBuffer(GL_ARRAY_BUFFER, my3DHead.vboID);
    glBufferData(GL_ARRAY_BUFFER, my3DHead.vertices.size() * sizeof(float), my3DHead.vertices.data(), GL_STATIC_DRAW);

    // 0. Position Attribute (3 floats, offset 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 1. Normal Attribute (3 floats, offset 3)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 2. Texture Coordinate Attribute (2 floats, offset 6)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // 3. Color Attribute (3 floats, offset 8)
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    // 4. Object ID Attribute (1 float, offset 11)
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void*)(11 * sizeof(float)));
    glEnableVertexAttribArray(4);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    my3DHead.isLoaded = true;
}

void HeadObject::RenderHeadToFramebuffer(ImVec2 viewportSize) {
    if (!my3DHead.isLoaded) return;

    int width = (int)viewportSize.x;
    int height = (int)viewportSize.y;
    if (width <= 0 || height <= 0) return;

    if (my3DHead.fboID == 0 || width != my3DHead.fboWidth || height != my3DHead.fboHeight) {
        my3DHead.fboWidth = width;
        my3DHead.fboHeight = height;

        if (my3DHead.fboID) glDeleteFramebuffers(1, &my3DHead.fboID);
        if (my3DHead.renderTextureID) glDeleteTextures(1, &my3DHead.renderTextureID);
        if (my3DHead.rboID) glDeleteRenderbuffers(1, &my3DHead.rboID);

        glGenFramebuffers(1, &my3DHead.fboID);
        glBindFramebuffer(GL_FRAMEBUFFER, my3DHead.fboID);

        glGenTextures(1, &my3DHead.renderTextureID);
        glBindTexture(GL_TEXTURE_2D, my3DHead.renderTextureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, my3DHead.renderTextureID, 0);

        // We use my3DHead.rboID so we can safely delete it when the window resizes!
        glGenRenderbuffers(1, &my3DHead.rboID);
        glBindRenderbuffer(GL_RENDERBUFFER, my3DHead.rboID);
        // Create a depth and stencil buffer for our FBO
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        // Attach it to the current FBO
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, my3DHead.rboID);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, my3DHead.fboID);
    glViewport(0, 0, my3DHead.fboWidth, my3DHead.fboHeight);
    glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(my3DHead.shaderProgram);

    int selLoc = glGetUniformLocation(my3DHead.shaderProgram, "uSelectedObjectID");
    glUniform1f(selLoc, static_cast<float>(selectedObjectID));

    int rotLoc = glGetUniformLocation(my3DHead.shaderProgram, "uRotation");
    glUniform2f(rotLoc, my3DHead.rotationX, my3DHead.rotationY);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, my3DHead.modelTextureID);

    glBindVertexArray(my3DHead.vaoID);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(my3DHead.vertices.size() / 12));

    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void HeadObject::Draw() {
    if (!my3DHead.isLoaded) return;

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 50 || viewportSize.y < 50) viewportSize = ImVec2(500, 500);

    RenderHeadToFramebuffer(viewportSize);
    ImGui::Image((void*)(intptr_t)my3DHead.renderTextureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));

    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        my3DHead.rotationY -= delta.x * 0.5f;
        my3DHead.rotationX += delta.y * 0.5f;
    }
}

void HeadObject::UpdateSubObjectScreenPositions(ImVec2 viewportSize, ImVec2 viewportPos) {
    // 1. Directly access my3DHead from inside the class!
    float cx = cos(my3DHead.rotationX * 3.14159f / 180.0f);
    float sx = sin(my3DHead.rotationX * 3.14159f / 180.0f);
    float cy = cos(my3DHead.rotationY * 3.14159f / 180.0f);
    float sy = sin(my3DHead.rotationY * 3.14159f / 180.0f);

    for (auto& obj : subObjects) {
        if (!obj.isSelectable) continue;

        float px = obj.centerX;
        float py = obj.centerY;
        float pz = obj.centerZ;

        // Y-axis rotation
        float x1 = px * cy - pz * sy;
        float z1 = px * sy + pz * cy;
        px = x1; pz = z1;

        // X-axis rotation
        float y2 = py * cx - pz * sx;
        float z2 = py * sx + pz * cx;
        py = y2; pz = z2;

        // 3. Map to 2D Screen Pixels
        float screenX = viewportPos.x + (px + 1.0f) * 0.5f * viewportSize.x;
        float screenY = viewportPos.y + (1.0f - py) * 0.5f * viewportSize.y;

        obj.screenPos = ImVec2(screenX, screenY);
    }
}