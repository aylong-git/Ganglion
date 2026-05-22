#include "HeadObject.h"
#include <iostream>

// Include tinyobjloader. 
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

void HeadObject::InitModel(const std::string& objFilePath, const std::string& mtlSearchPath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objFilePath.c_str(), mtlSearchPath.c_str());

    if (!warn.empty()) std::cout << "WARN: " << warn << "\n";
    if (!err.empty()) std::cout << "ERR: " << err << "\n";
    if (!ret) {
        std::cout << "Failed to load/parse .obj.\n";
        return;
    }

    // Extract raw vertex positions
    for (size_t s = 0; s < shapes.size(); s++) {
        for (size_t i = 0; i < shapes[s].mesh.indices.size(); i++) {
            tinyobj::index_t idx = shapes[s].mesh.indices[i];
            my3DHead.vertices.push_back(attrib.vertices[3 * size_t(idx.vertex_index) + 0]); // X
            my3DHead.vertices.push_back(attrib.vertices[3 * size_t(idx.vertex_index) + 1]); // Y
            my3DHead.vertices.push_back(attrib.vertices[3 * size_t(idx.vertex_index) + 2]); // Z
        }
    }

    my3DHead.isLoaded = true;

    // TODO: Initialize your Graphics API Framebuffer here (OpenGL, DirectX, etc.)
}

void HeadObject::RenderHeadToFramebuffer(ImVec2 viewportSize) {
    if (!my3DHead.isLoaded) return;

    // TODO: Graphics API specific code goes here.
    // 1. Bind the FBO (my3DHead.fboID)
    // 2. Set the viewport to match viewportSize
    // 3. Clear the background
    // 4. Apply rotation matrices using my3DHead.rotationX and rotationY
    // 5. Draw the vertices
    // 6. Unbind the FBO
}

void HeadObject::Draw() {
    ImGui::Text("Electrode Impedance Mapping");
    ImGui::Separator();

    if (!my3DHead.isLoaded) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load 3D Head model.");
        return;
    }

    // 1. Define the size of our 3D view area
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    // 2. Render the 3D scene into our hidden texture (based on current rotations)
    RenderHeadToFramebuffer(viewportSize);

    // 3. Display the texture in ImGui
    // The cast to (void*)(intptr_t) is required by ImGui for texture IDs
    ImGui::Image((void*)(intptr_t)my3DHead.renderTextureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));

    // 4. Handle Mouse Interaction for Rotation
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        // Adjust these multipliers to change rotation sensitivity
        my3DHead.rotationY += delta.x * 0.5f;
        my3DHead.rotationX += delta.y * 0.5f;
    }

    ImGui::Text("Drag the 3D model to rotate.");
    ImGui::Text("Current Rotation: X: %.1f, Y: %.1f", my3DHead.rotationX, my3DHead.rotationY);
}