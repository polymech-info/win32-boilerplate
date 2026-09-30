# pragma once
# include <imgui.h>
# include <string>
# include <memory>
# include <map>
# include <vector>

# include "app-types.h"


struct WindowState
{
    int x = -1;
    int y = -1;
    int width = 1440;
    int height = 800;
    int monitor = 0;
    bool maximized = false;
};

struct Platform;
struct Renderer;

struct Application
{
    Application(const char* name);
    Application(const char* name, const ArgsMap& args);
    ~Application();

    bool Create(int width = -1, int height = -1);

    int Run();

    void SetTitle(const char* title);

    bool Close();
    void Quit();

    const std::string& GetName() const;

    ImFont* DefaultFont() const;
    ImFont* HeaderFont() const;

    ImTextureID LoadTexture(const char* path);
    ImTextureID CreateTexture(const void* data, int width, int height);
    void        DestroyTexture(ImTextureID texture);
    int         GetTextureWidth(ImTextureID texture);
    int         GetTextureHeight(ImTextureID texture);

    bool TakeScreenshot(const char* filename = nullptr);

    bool SaveWindowState();
    bool LoadWindowState();

    virtual void OnStart() {}
    virtual void OnStop() {}
    virtual void OnFrame(float deltaTime) {}

    virtual ImGuiWindowFlags GetWindowFlags() const;

    virtual bool CanClose() { return true; }

protected:
    ArgsMap                     m_Args;  // CLI arguments (accessible to derived classes)

private:
    void RecreateFontAtlas();

    void Frame();

    std::string                 m_Name;
    std::string                 m_IniFilename;
    std::string                 m_WindowStateFilename;
    std::unique_ptr<Platform>   m_Platform;
    std::unique_ptr<Renderer>   m_Renderer;
    ImGuiContext*               m_Context = nullptr;
    ImFont*                     m_DefaultFont = nullptr;
    ImFont*                     m_HeaderFont = nullptr;
    WindowState                 m_WindowState;
};

extern Application* g_Application;

int Main(const ArgsMap& args);