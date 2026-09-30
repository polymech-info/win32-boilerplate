#ifndef PM_UI_NODESPANEL_H
#define PM_UI_NODESPANEL_H

#include "stdafx.h"

#ifdef FEATURE_NODES
#include <d3d11.h>
struct ImGuiContext;
class App;
#endif

/////////////////////////////////////////////////////////
// CNodesView — when FEATURE_NODES is enabled, hosts a
// DX11 + Dear ImGui rendering surface for the NodeHub
// graph editor.  Otherwise a static placeholder.
class CNodesView : public CWnd
{
public:
    CNodesView() = default;
    virtual ~CNodesView() override;

protected:
    virtual int     OnCreate(CREATESTRUCT& cs) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;
#ifdef FEATURE_NODES
    virtual void    OnDestroy() override;
#endif

private:
    CNodesView(const CNodesView&) = delete;
    CNodesView& operator=(const CNodesView&) = delete;

#ifdef FEATURE_NODES
    bool InitD3D(HWND hWnd);
    void CleanupD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void RenderFrame();
    void ResizeSwapChain(int width, int height);

    static LRESULT CALLBACK RenderWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND                    m_hRenderWnd = nullptr;
    WNDCLASSEXW             m_renderWndClass{};

    ID3D11Device*           m_pd3dDevice = nullptr;
    ID3D11DeviceContext*    m_pd3dContext = nullptr;
    IDXGISwapChain*         m_pSwapChain = nullptr;
    ID3D11RenderTargetView* m_pRTV = nullptr;

    ImGuiContext*           m_imguiCtx = nullptr;
    bool                    m_d3dReady = false;

    App*                    m_app = nullptr;
    bool                    m_appStarted = false;

    static constexpr UINT_PTR TIMER_RENDER = 1;
    static constexpr UINT     FRAME_INTERVAL_MS = 16;
#else
    HWND m_hLabel{};
#endif
};

/////////////////////////////////////////////////////////
// CNodesContainer — dock container hosting CNodesView.
class CNodesContainer : public CDockContainer
{
public:
    CNodesContainer();
    virtual ~CNodesContainer() override = default;
    CNodesView& GetNodesView() { return m_view; }

protected:
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CNodesContainer(const CNodesContainer&) = delete;
    CNodesContainer& operator=(const CNodesContainer&) = delete;
    CNodesView m_view;
};

/////////////////////////////////////////////////////////
// CDockNodes — docker wrapping CNodesContainer.
class CDockNodes : public CDocker
{
public:
    CDockNodes();
    virtual ~CDockNodes() override = default;
    CNodesContainer& GetNodesContainer() { return m_container; }

protected:
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CDockNodes(const CDockNodes&) = delete;
    CDockNodes& operator=(const CDockNodes&) = delete;
    CNodesContainer m_container;
};

#endif // PM_UI_NODESPANEL_H
