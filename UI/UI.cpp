#include "UI.h"
#include "imgui.h"
#include "implot.h"
#include <vector>

UI::UI(BrainflowAlgorithm& backendRef) : backend(backendRef) {
    // Pre-fill the plot data with zeros so it doesn't crash on the first frame
    timeData.resize(maxPlotPoints, 0.0f);
    concentrationData.resize(maxPlotPoints, 0.0f);
}

void UI::Render() {
    // 1. Create the main window
    ImGui::Begin("Ganglion BCI Controller");

    // 2. Connection Settings (Replaces Tkinter Entry widgets)
    ImGui::Text("Connection Settings");
    ImGui::InputText("COM Port", portBuffer, sizeof(portBuffer));
    ImGui::InputText("MAC Address", macBuffer, sizeof(macBuffer));

    // Connect / Disconnect Buttons
    if (!backend.isConnected) {
        if (ImGui::Button("Connect", ImVec2(120, 30))) {
            // Gather the selected channels based on Ganglion's index (Rows 1-4)
            std::vector<int> activeChannels;
            if (channel1) activeChannels.push_back(1);
            if (channel2) activeChannels.push_back(2);
            if (channel3) activeChannels.push_back(3);
            if (channel4) activeChannels.push_back(4);

            backend.Connect(portBuffer, macBuffer, activeChannels);
        }
    }
    else {
        if (ImGui::Button("Disconnect", ImVec2(120, 30))) {
            backend.Disconnect();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: CONNECTED & STREAMING");
    }

    ImGui::Separator();

    // 3. Tab System (Replaces ttk.Notebook)
    if (ImGui::BeginTabBar("MainTabs")) {

        // --- CONTROL TAB ---
        if (ImGui::BeginTabItem("Control")) {

            // Channel Selection Checkboxes
            ImGui::Text("Active EEG Channels:");
            ImGui::Checkbox("Ch 1", &channel1); ImGui::SameLine();
            ImGui::Checkbox("Ch 2", &channel2); ImGui::SameLine();
            ImGui::Checkbox("Ch 3", &channel3); ImGui::SameLine();
            ImGui::Checkbox("Ch 4", &channel4);

            ImGui::Spacing();

            // Threshold Slider
            ImGui::SliderFloat("Focus Threshold", &threshold, 0.0f, 1.0f);

            // Read the real-time concentration from the backend
            float currentConcentration = (float)backend.concentrationMeasure;
            ImGui::Text("Current Concentration Level: %.2f", currentConcentration);

            // Rolling Buffer Logic (Updates the graph 10 times a second)
            float currentTime = (float)ImGui::GetTime();
            if (currentTime - lastPlotUpdateTime > 0.1f) {
                // Erase the oldest point
                timeData.erase(timeData.begin());
                concentrationData.erase(concentrationData.begin());

                // Add the newest point
                timeData.push_back(timeElapsed);
                concentrationData.push_back(currentConcentration);

                timeElapsed += 0.1f;
                lastPlotUpdateTime = currentTime;
            }

            // Draw the Graph (Replaces Matplotlib FigureCanvasTkAgg)
            if (ImPlot::BeginPlot("Concentration Timeline", ImVec2(-1, 300))) {
                // Lock the Y-axis from 0 to 1 (since ML predicts probabilities)
                ImPlot::SetupAxes("Time (s)", "Focus Level", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_LockMin | ImPlotAxisFlags_LockMax);
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0);

                // Plot the actual data line
                ImPlot::PlotLine("Concentration", timeData.data(), concentrationData.data(), maxPlotPoints);

                // Plot the Threshold line for visual reference
                double threshLineX[2] = { timeData.front(), timeData.back() };
                double threshLineY[2] = { threshold, threshold };

                ImPlotSpec spec;
                spec.LineColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                spec.LineWeight = 2.0f;
                ImPlot::PlotLine("Threshold Target", threshLineX, threshLineY, 2, spec);

                ImPlot::EndPlot();
            }

            ImGui::EndTabItem();
        }

        // --- IMPEDANCE TAB ---
        if (ImGui::BeginTabItem("Impedance")) {
            ImGui::Text("Impedance checking logic will go here.");
            ImGui::TextWrapped("Note: We will implement the Windows BLE custom impedance checker once the main data stream is validated.");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}