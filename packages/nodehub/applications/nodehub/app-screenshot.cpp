#include "app.h"
#include <imgui.h>
#include <cstdio>
#include <string>
#include <ctime>


void App::TakeScreenshot(const char* filename)
{
    printf("\n========== SCREENSHOT REQUEST ==========\n");
    printf("App::TakeScreenshot called with filename: %s\n", filename ? filename : "nullptr");
    
    // Generate default filename if not provided
    std::string fname;
    if (!filename)
    {
        time_t now = time(nullptr);
        char buffer[64];
        strftime(buffer, sizeof(buffer), "screenshot_%Y%m%d_%H%M%S.png", localtime(&now));
        fname = buffer;
        printf("App::TakeScreenshot - Generated filename: %s\n", fname.c_str());
    }
    else
    {
        fname = filename;
        printf("App::TakeScreenshot - Using provided filename: %s\n", fname.c_str());
    }
    
    
    // Delegate to Application base class which calls the renderer
    bool success = Application::TakeScreenshot(fname.c_str());
    
    printf("App::TakeScreenshot - Result: %s\n", success ? "SUCCESS" : "FAILED");
    printf("==========================================\n\n");
    
    if (success)
    {
        m_ScreenshotMessage = "Screenshot saved: " + fname;
        m_ScreenshotMessageTime = 5.0f;
        
    }
    else
    {
        m_ScreenshotMessage = "Failed to save screenshot: " + fname;
        m_ScreenshotMessageTime = 5.0f;
        
    }
}

void App::RenderScreenshotNotification(float deltaTime)
{
    if (m_ScreenshotMessageTime > 0.0f)
    {
        m_ScreenshotMessageTime -= deltaTime;
        
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.7f); // More visible background
        if (ImGui::Begin("Screenshot Notification", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            // Use different color based on success/failure
            if (m_ScreenshotMessage.find("FAILED") != std::string::npos)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); // Red for error
                ImGui::Text("%s", m_ScreenshotMessage.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f)); // Green for success
                ImGui::Text("%s", m_ScreenshotMessage.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();
    }
}
