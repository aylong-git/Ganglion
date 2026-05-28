#pragma once
#include <vector>
#include <string>
#include "imgui.h"

// A structure to hold everything related to our 3D Head
struct HeadModelData {
    bool isLoaded = false;
    std::vector<float> vertices;

    unsigned int vaoID = 0;
    unsigned int vboID = 0;
    unsigned int fboID = 0;
    unsigned int renderTextureID = 0;
    unsigned int rboID = 0;
    unsigned int shaderProgram = 0;

    // NEW: The ID for your loaded skin texture!
    unsigned int modelTextureID = 0;

    int fboWidth = 0;
    int fboHeight = 0;

    float rotationX = 0.0f;
    float rotationY = 0.0f;
    float meshCenter[3] = { 0.0f, 0.0f, 0.0f };
};

struct SubObject {
    int id;
    std::string name;
    std::string description;
    float centerX, centerY, centerZ;
    ImVec2 screenPos;
    bool isSelectable;               // NEW: Should the mouse ignore this?
};

class HeadObject {
private:
    HeadModelData my3DHead;

    void RenderHeadToFramebuffer(ImVec2 viewportSize);
    void SetupShaders(); // Helper to compile a basic visual look
    

public:
    HeadObject() = default;
    ~HeadObject(); // Added to clean up GPU leaks on close

    void InitModel(const std::string& objFilePath, const std::string& mtlSearchPath);
    void UpdateSubObjectScreenPositions(ImVec2 viewportSize, ImVec2 viewportPos);
    void Draw();
    
    std::vector<SubObject> subObjects;
    int selectedObjectID = -1;
};