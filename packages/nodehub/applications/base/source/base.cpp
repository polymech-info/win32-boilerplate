# include "base.h"
# include "setup.h"
# include "platform.h"
# include "renderer.h"
# include <vector>
# include <fstream>
# include <sstream>

#ifdef _WIN32
#   include <windows.h>
#   include <io.h>
#   include <fcntl.h>
#   include <iostream>
#   include <cstdio>
#endif

namespace {

#ifdef _WIN32
static inline std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr) return {};
    // Ask for required size (includes NUL)
    const int n = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out;
    out.resize(n - 1); // we store without the trailing NUL

#if __cplusplus >= 201703L
    // C++17: string::data() is char*
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), n, nullptr, nullptr);
#else
    // C++11/14: use &out[0] to get char*
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], n, nullptr, nullptr);
#endif

    // The call above wrote a NUL at the end because we passed length n.
    // Keep size at n-1 (already set via resize).
    return out;
}

static std::string GetExecutableDir()
{
    wchar_t exe_path_buffer[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(NULL, exe_path_buffer, MAX_PATH) == 0)
        return "";

    std::wstring exe_path(exe_path_buffer);
    size_t last_slash = exe_path.find_last_of(L"\\/");
    if (last_slash == std::wstring::npos)
        return "";

    return WideToUtf8(exe_path.substr(0, last_slash).c_str());
}
#else
static std::string GetExecutableDir()
{
    // Implementation for other platforms (e.g., Linux, macOS) would go here.
    return "";
}
#endif

static std::string ResolveResourcePath(const std::string& relativePath)
{
    static const std::string exeDir = GetExecutableDir();
    if (!exeDir.empty())
    {
        std::string exePath = exeDir + "/" + relativePath;
        std::ifstream f(exePath);
        if (f.good())
        {
            return exePath;
        }
    }

    // Fallback to CWD
    return relativePath;
}

} // namespace


extern "C" {
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"
}


Application::Application(const char* name)
    : Application(name, {}){}

Application::Application(const char* name, const ArgsMap& args)
    : m_Name(name)
    , m_Args(args)
    , m_Platform(CreatePlatform(*this))
    , m_Renderer(CreateRenderer())
{
    g_Application = this;
    
    // Convert map to argc/argv for platform compatibility
    std::vector<std::string> argv_vec;
    argv_vec.push_back(name); // program name
    
    for (const auto& pair : args) {
        if (!pair.first.empty()) {
            argv_vec.push_back("--" + pair.first);
            const auto& value = pair.second;
            if (value.Type != ArgValue::Type::Empty)
            {
                if (value.Type == ArgValue::Type::String)
                    argv_vec.push_back(value.String);
                else if (value.Type == ArgValue::Type::Bool)
                    argv_vec.push_back(value.Bool ? "true" : "false");
                else if (value.Type == ArgValue::Type::Int)
                    argv_vec.push_back(std::to_string(value.Int));
                else if (value.Type == ArgValue::Type::Double)
                    argv_vec.push_back(std::to_string(value.Double));
            }
        }
    }
    
    // Convert to char** for platform
    std::vector<char*> argv_ptrs;
    for (auto& str : argv_vec) {
        argv_ptrs.push_back(const_cast<char*>(str.c_str()));
    }
    
    m_Platform->ApplicationStart(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
}

Application::~Application()
{
    g_Application = nullptr;

    // Save window state before cleanup
    if (m_Platform)
    {
        if (!SaveWindowState())
        {

        }
    }

    m_Renderer->Destroy();

    m_Platform->ApplicationStop();

    if (m_Context)
    {
        ImGui::DestroyContext(m_Context);
        m_Context= nullptr;
    }
}

bool Application::Create(int width /*= -1*/, int height /*= -1*/)
{
    m_Context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_Context);

    // Set filenames first
    m_IniFilename = m_Name + ".ini";
    m_WindowStateFilename = m_Name + "_window.json";

    // Load saved window state
    if (LoadWindowState())
    {
        // Use saved dimensions if not explicitly provided
        if (width < 0)
            width = m_WindowState.width;
        if (height < 0)
            height = m_WindowState.height;
    }
    else
    {
        
    }

    if (!m_Platform->OpenMainWindow("NodeHub", width, height))
        return false;

    // Restore window position/monitor after creation
    if (m_WindowState.x >= 0 && m_WindowState.y >= 0)
    {
        m_Platform->SetWindowState(m_WindowState);
    }

    if (!m_Renderer->Create(*m_Platform))
        return false;

    ImGuiIO& io = ImGui::GetIO();
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.IniFilename = m_IniFilename.c_str();
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    RecreateFontAtlas();
    m_Platform->AcknowledgeWindowScaleChanged();
    m_Platform->AcknowledgeFramebufferScaleChanged();

    OnStart();
    Frame();
    return true;
}

int Application::Run()
{
    m_Platform->ShowMainWindow();

    while (m_Platform->ProcessMainWindowEvents())
    {
        if (!m_Platform->IsMainWindowVisible())
            continue;

        Frame();
    }

    OnStop();

    return 0;
}

void Application::RecreateFontAtlas()
{
    ImGuiIO& io = ImGui::GetIO();

    IM_DELETE(io.Fonts);

    io.Fonts = IM_NEW(ImFontAtlas);

    ImFontConfig config;
    config.OversampleH = 4;
    config.OversampleV = 4;
    config.PixelSnapH = false;

    m_DefaultFont = io.Fonts->AddFontFromFileTTF(ResolveResourcePath("data/Play-Regular.ttf").c_str(), 18.0f, &config);
    m_HeaderFont  = io.Fonts->AddFontFromFileTTF(ResolveResourcePath("data/Cuprum-Bold.ttf").c_str(),  20.0f, &config);

    io.Fonts->Build();
}

void Application::Frame()
{
    auto& io = ImGui::GetIO();

    if (m_Platform->HasWindowScaleChanged())
        m_Platform->AcknowledgeWindowScaleChanged();

    if (m_Platform->HasFramebufferScaleChanged())
    {
        RecreateFontAtlas();
        m_Platform->AcknowledgeFramebufferScaleChanged();
    }

    const float windowScale      = m_Platform->GetWindowScale();
    const float framebufferScale = m_Platform->GetFramebufferScale();

    if (io.WantSetMousePos)
    {
        io.MousePos.x *= windowScale;
        io.MousePos.y *= windowScale;
    }

    m_Platform->NewFrame();

    // Don't touch "uninitialized" mouse position
    if (io.MousePos.x > -FLT_MAX && io.MousePos.y > -FLT_MAX)
    {
        io.MousePos.x    /= windowScale;
        io.MousePos.y    /= windowScale;
    }
    io.DisplaySize.x /= windowScale;
    io.DisplaySize.y /= windowScale;

    io.DisplayFramebufferScale.x = framebufferScale;
    io.DisplayFramebufferScale.y = framebufferScale;

    m_Renderer->NewFrame();

    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const auto windowBorderSize = ImGui::GetStyle().WindowBorderSize;
    const auto windowRounding   = ImGui::GetStyle().WindowRounding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("Content", nullptr, GetWindowFlags());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, windowBorderSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, windowRounding);

    OnFrame(io.DeltaTime);

    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleVar(2);

    // Rendering
    m_Renderer->Clear(ImColor(32, 32, 32, 255));
    ImGui::Render();
    m_Renderer->RenderDrawData(ImGui::GetDrawData());

    m_Platform->FinishFrame();
}

void Application::SetTitle(const char* title)
{
    m_Platform->SetMainWindowTitle(title);
}

bool Application::Close()
{
    return m_Platform->CloseMainWindow();
}

void Application::Quit()
{
    m_Platform->Quit();
}

const std::string& Application::GetName() const
{
    return m_Name;
}

ImFont* Application::DefaultFont() const
{
    return m_DefaultFont;
}

ImFont* Application::HeaderFont() const
{
    return m_HeaderFont;
}

ImTextureID Application::LoadTexture(const char* path)
{
    int width = 0, height = 0, component = 0;
    if (auto data = stbi_load(ResolveResourcePath(path).c_str(), &width, &height, &component, 4))
    {
        auto texture = CreateTexture(data, width, height);
        stbi_image_free(data);
        return texture;
    }
    else
        return nullptr;
}

ImTextureID Application::CreateTexture(const void* data, int width, int height)
{
    return m_Renderer->CreateTexture(data, width, height);
}

void Application::DestroyTexture(ImTextureID texture)
{
    m_Renderer->DestroyTexture(texture);
}

int Application::GetTextureWidth(ImTextureID texture)
{
    return m_Renderer->GetTextureWidth(texture);
}

int Application::GetTextureHeight(ImTextureID texture)
{
    return m_Renderer->GetTextureHeight(texture);
}

bool Application::TakeScreenshot(const char* filename)
{
    return m_Renderer->TakeScreenshot(filename);
}

bool Application::SaveWindowState()
{
    if (!m_Platform)
        return false;
    
    auto state = m_Platform->GetWindowState();
    
    std::ofstream file(m_WindowStateFilename);
    if (!file)
        return false;
    
    file << "{\n";
    file << "  \"window\": {\n";
    file << "    \"x\": " << state.x << ",\n";
    file << "    \"y\": " << state.y << ",\n";
    file << "    \"width\": " << state.width << ",\n";
    file << "    \"height\": " << state.height << ",\n";
    file << "    \"monitor\": " << state.monitor << ",\n";
    file << "    \"maximized\": " << (state.maximized ? "true" : "false") << "\n";
    file << "  }\n";
    file << "}\n";
    
    return true;
}

bool Application::LoadWindowState()
{
    std::ifstream file(m_WindowStateFilename);
    if (!file)
        return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Simple JSON parsing for our specific structure
    auto findValue = [&content](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = content.find(searchKey);
        if (pos == std::string::npos)
            return "";
        
        pos += searchKey.length();
        while (pos < content.length() && (content[pos] == ' ' || content[pos] == '\t'))
            pos++;
        
        size_t endPos = pos;
        while (endPos < content.length() && content[endPos] != ',' && content[endPos] != '\n' && content[endPos] != '}')
            endPos++;
        
        return content.substr(pos, endPos - pos);
    };
    
    try {
        std::string xStr = findValue("x");
        std::string yStr = findValue("y");
        std::string widthStr = findValue("width");
        std::string heightStr = findValue("height");
        std::string monitorStr = findValue("monitor");
        std::string maximizedStr = findValue("maximized");
        
        if (!xStr.empty()) m_WindowState.x = std::stoi(xStr);
        if (!yStr.empty()) m_WindowState.y = std::stoi(yStr);
        if (!widthStr.empty()) m_WindowState.width = std::stoi(widthStr);
        if (!heightStr.empty()) m_WindowState.height = std::stoi(heightStr);
        if (!monitorStr.empty()) m_WindowState.monitor = std::stoi(monitorStr);
        if (!maximizedStr.empty()) m_WindowState.maximized = (maximizedStr.find("true") != std::string::npos);
        
        return true;
    }
    catch (...) {
        // If parsing fails, return false and use defaults
        return false;
    }
}

ImGuiWindowFlags Application::GetWindowFlags() const
{
    return
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
}
