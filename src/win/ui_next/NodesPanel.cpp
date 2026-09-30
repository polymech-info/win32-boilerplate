#include "stdafx.h"
#include "NodesPanel.h"
#include "win/settings_store.hpp"
#include "helpers/dock_chrome_i18n.hpp"
#include "helpers/ui_font.hpp"

#ifdef FEATURE_NODES

#include <imgui.h>
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma push_macro("E_NOTIMPL")
#pragma push_macro("E_ACCESSDENIED")
#pragma push_macro("E_FAIL")
#undef E_NOTIMPL
#undef E_ACCESSDENIED
#undef E_FAIL
#include "../../../packages/nodehub/applications/nodehub/app.h"
#include "../../../packages/nodehub/applications/base/include/app-types.h"
#pragma pop_macro("E_FAIL")
#pragma pop_macro("E_ACCESSDENIED")
#pragma pop_macro("E_NOTIMPL")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Application* g_Application = nullptr;

//////////////////////////////////////////
// CNodesView — FEATURE_NODES enabled
//////////////////////////////////////////

CNodesView::~CNodesView()
{
    CleanupD3D();
}

LRESULT CALLBACK CNodesView::RenderWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<CNodesView*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));

    if (self && self->m_imguiCtx) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(self->m_imguiCtx);
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
            ImGui::SetCurrentContext(prev);
            return 1;
        }
        ImGui::SetCurrentContext(prev);
    }

    switch (msg) {
    case WM_SIZE:
        if (self && self->m_d3dReady && wParam != SIZE_MINIMIZED)
            self->ResizeSwapChain(LOWORD(lParam), HIWORD(lParam));
        return 0;
    }

    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int CNodesView::OnCreate(CREATESTRUCT&)
{
    m_renderWndClass = {};
    m_renderWndClass.cbSize = sizeof(WNDCLASSEXW);
    m_renderWndClass.style = CS_OWNDC;
    m_renderWndClass.lpfnWndProc = RenderWndProc;
    m_renderWndClass.hInstance = ::GetModuleHandleW(nullptr);
    m_renderWndClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    m_renderWndClass.lpszClassName = L"PM_NodesView_DX11";
    ::RegisterClassExW(&m_renderWndClass);

    CRect rc = GetClientRect();
    m_hRenderWnd = ::CreateWindowExW(
        0, L"PM_NodesView_DX11", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, rc.Width(), rc.Height(),
        GetHwnd(), nullptr, m_renderWndClass.hInstance, nullptr);

    if (!m_hRenderWnd)
        return -1;

    ::SetWindowLongPtr(m_hRenderWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (!InitD3D(m_hRenderWnd)) {
        ::DestroyWindow(m_hRenderWnd);
        m_hRenderWnd = nullptr;
        return -1;
    }

    m_imguiCtx = ImGui::CreateContext();
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(m_hRenderWnd);
    ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dContext);

    try {
        ArgsMap args;
        m_app = new App("NodeHub", args);
        m_app->OnStart();
        m_appStarted = true;
    } catch (...) {
        m_appStarted = false;
    }

    ImGui::SetCurrentContext(prev);

    ::SetTimer(GetHwnd(), TIMER_RENDER, FRAME_INTERVAL_MS, nullptr);
    return 0;
}

void CNodesView::OnDestroy()
{
    ::KillTimer(GetHwnd(), TIMER_RENDER);

    if (m_imguiCtx) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);

        if (m_app) {
            if (m_appStarted)
                m_app->OnStop();
            delete m_app;
            m_app = nullptr;
            m_appStarted = false;
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::SetCurrentContext(prev);
        ImGui::DestroyContext(m_imguiCtx);
        m_imguiCtx = nullptr;
    }

    CleanupD3D();

    if (m_hRenderWnd) {
        ::DestroyWindow(m_hRenderWnd);
        m_hRenderWnd = nullptr;
    }
    ::UnregisterClassW(L"PM_NodesView_DX11", ::GetModuleHandleW(nullptr));
}

bool CNodesView::InitD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 1, D3D11_SDK_VERSION,
        &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dContext);

    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    m_d3dReady = true;
    return true;
}

void CNodesView::CleanupD3D()
{
    m_d3dReady = false;
    CleanupRenderTarget();
    if (m_pSwapChain)   { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
    if (m_pd3dContext)  { m_pd3dContext->Release(); m_pd3dContext = nullptr; }
    if (m_pd3dDevice)   { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

void CNodesView::CreateRenderTarget()
{
    ID3D11Texture2D* backBuf = nullptr;
    m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuf);
    if (backBuf) {
        m_pd3dDevice->CreateRenderTargetView(backBuf, nullptr, &m_pRTV);
        backBuf->Release();
    }
}

void CNodesView::CleanupRenderTarget()
{
    if (m_pRTV) { m_pRTV->Release(); m_pRTV = nullptr; }
}

void CNodesView::ResizeSwapChain(int width, int height)
{
    if (!m_pSwapChain || width <= 0 || height <= 0)
        return;

    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui::SetCurrentContext(prev);

    CleanupRenderTarget();
    m_pSwapChain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void CNodesView::RenderFrame()
{
    if (!m_d3dReady || !m_imguiCtx)
        return;

    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();

    if (m_app && m_appStarted) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        const auto wbs = ImGui::GetStyle().WindowBorderSize;
        const auto wr  = ImGui::GetStyle().WindowRounding;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("Content", nullptr, m_app->GetWindowFlags());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, wbs);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, wr);

        m_app->OnFrame(io.DeltaTime);

        ImGui::PopStyleVar(2);
        ImGui::End();
        ImGui::PopStyleVar(2);
    } else {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##NodeHub", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::Text("NodeHub failed to initialize.");
        ImGui::End();
    }

    const float clear[] = { 0.12f, 0.12f, 0.14f, 1.0f };
    m_pd3dContext->OMSetRenderTargets(1, &m_pRTV, nullptr);
    m_pd3dContext->ClearRenderTargetView(m_pRTV, clear);

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_pSwapChain->Present(1, 0);

    ImGui::SetCurrentContext(prev);
}

LRESULT CNodesView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case WM_SIZE:
            if (m_hRenderWnd) {
                int w = LOWORD(lparam), h = HIWORD(lparam);
                ::MoveWindow(m_hRenderWnd, 0, 0, w, h, TRUE);
            }
            break;
        case WM_TIMER:
            if (wparam == TIMER_RENDER) {
                RenderFrame();
                return 0;
            }
            break;
        }
        return WndProcDefault(msg, wparam, lparam);
    }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

#else // !FEATURE_NODES

//////////////////////////////////////////
// CNodesView — placeholder (FEATURE_NODES off)
//////////////////////////////////////////

CNodesView::~CNodesView() = default;

int CNodesView::OnCreate(CREATESTRUCT&)
{
    CRect rc = GetClientRect();
    m_hLabel = ::CreateWindowExW(
        0, L"STATIC",
        L"NodeHub is disabled (FEATURE_NODES=OFF).\n"
        L"Rebuild with -DFEATURE_NODES=ON to enable.",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        0, 0, rc.Width(), rc.Height(),
        GetHwnd(), nullptr, ::GetModuleHandleW(nullptr), nullptr);
    HFONT hFont = pmui::ui_font();
    ::SendMessageW(m_hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    return 0;
}

LRESULT CNodesView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        if (msg == WM_SIZE && m_hLabel) {
            int w = LOWORD(lparam), h = HIWORD(lparam);
            ::MoveWindow(m_hLabel, 0, 0, w, h, TRUE);
        }
        return WndProcDefault(msg, wparam, lparam);
    }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

#endif // FEATURE_NODES

//////////////////////////////////////////
// CNodesContainer
//////////////////////////////////////////

CNodesContainer::CNodesContainer()
{
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    SetTabText(d.nodes_tab);
    SetDockCaption(d.nodes_caption);
    SetView(m_view);
}

LRESULT CNodesContainer::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try { return WndProcDefault(msg, wparam, lparam); }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

//////////////////////////////////////////
// CDockNodes
//////////////////////////////////////////

CDockNodes::CDockNodes()
{
    SetView(m_container);
    SetBarWidth(3);
    SetBarColor(RGB(204, 206, 210));
}

LRESULT CDockNodes::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try { return WndProcDefault(msg, wparam, lparam); }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}
