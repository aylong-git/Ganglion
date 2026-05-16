#include <iostream>

// ImGui, ImPlot, and GLFW headers
#include "imgui.h"
#include "implot.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h> 

// Your custom application headers
#include "BrainflowAlgorithm.h"
#include "UI.h"

// Simple callback to catch any window-creation errors
static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

int main(int, char**) {
    // 1. --- INITIALIZE THE GRAPHICS WINDOW (GLFW) ---
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Set up OpenGL version (GL 3.0 + GLSL 130)
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create the actual window
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Ganglion BCI Controller", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync (Caps framerate to your monitor's refresh rate)

    // 2. --- INITIALIZE DEAR IMGUI & IMPLOT ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext(); // Essential: Initialize ImPlot for your graphs

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark(); // Set a nice dark theme

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // 3. --- INSTANTIATE YOUR OOP CLASSES ---
    // Create the backend (which manages the BrainFlow thread)
    BrainflowAlgorithm backend;

    // Create the frontend UI, passing it the backend so it can read the data
    UI frontend(backend);

    // 4. --- THE MAIN APPLICATION LOOP ---
    // This loop runs continuously until you close the window
    while (!glfwWindowShouldClose(window)) {
        // Poll for window events (like mouse clicks or key presses)
        glfwPollEvents();

        // Start a new ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- DRAW YOUR UI ---
        // This calls the Render() function we wrote in UI.cpp
        frontend.Render();

        // --- RENDER TO SCREEN ---
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        // Background color of the window behind your ImGui panels
        glClearColor(0.15f, 0.15f, 0.15f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // 5. --- CLEANUP & TEARDOWN ---
    // Safely stop the Brainflow thread if it's still running
    backend.Disconnect();

    // Destroy ImGui/ImPlot instances and close the window
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}