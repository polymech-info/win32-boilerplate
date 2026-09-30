# include "renderer.h"
# include "setup.h"

# if RENDERER(IMGUI_DX11)

# include "platform.h"

# if PLATFORM(WINDOWS)
#     define NOMINMAX
#     define WIN32_LEAN_AND_MEAN
#     include <windows.h>
# endif

# include <imgui.h>
# include "imgui_impl_dx11.h"
# include <d3d11.h>
# include <vector>
# include <string>
# include <ctime>
# include <cstring>

# define STB_IMAGE_WRITE_IMPLEMENTATION
# include "../../external/stb_image_latest/stb_image_write.h"


struct RendererDX11 final
    : Renderer
{
    bool Create(Platform& platform) override;
    void Destroy() override;
    void NewFrame() override;
    void RenderDrawData(ImDrawData* drawData) override;
    void Clear(const ImVec4& color) override;
    void Present() override;
    void Resize(int width, int height) override;

    ImTextureID CreateTexture(const void* data, int width, int height) override;
    void        DestroyTexture(ImTextureID texture) override;
    int         GetTextureWidth(ImTextureID texture) override;
    int         GetTextureHeight(ImTextureID texture) override;
    bool        TakeScreenshot(const char* filename) override;

    HRESULT CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();

    void CreateRenderTarget();
    void CleanupRenderTarget();

    Platform*               m_Platform = nullptr;
    ID3D11Device*           m_device = nullptr;
    ID3D11DeviceContext*    m_deviceContext = nullptr;
    IDXGISwapChain*         m_swapChain = nullptr;
    ID3D11RenderTargetView* m_mainRenderTargetView = nullptr;
};

std::unique_ptr<Renderer> CreateRenderer()
{
    return std::make_unique<RendererDX11>();
}

bool RendererDX11::Create(Platform& platform)
{
    m_Platform = &platform;

    auto hr = CreateDeviceD3D(reinterpret_cast<HWND>(platform.GetMainWindowHandle()));
    if (FAILED(hr))
        return false;

    if (!ImGui_ImplDX11_Init(m_device, m_deviceContext))
    {
        CleanupDeviceD3D();
        return false;
    }

    m_Platform->SetRenderer(this);

    return true;
}

void RendererDX11::Destroy()
{
    if (!m_Platform)
        return;

    m_Platform->SetRenderer(nullptr);

    ImGui_ImplDX11_Shutdown();

    CleanupDeviceD3D();
}

void RendererDX11::NewFrame()
{
    ImGui_ImplDX11_NewFrame();
}

void RendererDX11::RenderDrawData(ImDrawData* drawData)
{
    ImGui_ImplDX11_RenderDrawData(drawData);
}

void RendererDX11::Clear(const ImVec4& color)
{
    m_deviceContext->ClearRenderTargetView(m_mainRenderTargetView, (float*)&color.x);
}

void RendererDX11::Present()
{
    m_swapChain->Present(1, 0);
}

void RendererDX11::Resize(int width, int height)
{
    ImGui_ImplDX11_InvalidateDeviceObjects();
    CleanupRenderTarget();
    m_swapChain->ResizeBuffers(0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

HRESULT RendererDX11::CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
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
    }

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &m_swapChain, &m_device, &featureLevel, &m_deviceContext) != S_OK)
        return E_FAIL;

    CreateRenderTarget();

    return S_OK;
}

void RendererDX11::CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_deviceContext) { m_deviceContext->Release(); m_deviceContext = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
}

void RendererDX11::CreateRenderTarget()
{
    DXGI_SWAP_CHAIN_DESC sd;
    m_swapChain->GetDesc(&sd);

    // Create the render target
    ID3D11Texture2D* pBackBuffer;
    D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
    ZeroMemory(&render_target_view_desc, sizeof(render_target_view_desc));
    render_target_view_desc.Format = sd.BufferDesc.Format;
    render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    m_device->CreateRenderTargetView(pBackBuffer, &render_target_view_desc, &m_mainRenderTargetView);
    m_deviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
    pBackBuffer->Release();
}

void RendererDX11::CleanupRenderTarget()
{
    if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
}

ImTextureID RendererDX11::CreateTexture(const void* data, int width, int height)
{
    return ImGui_CreateTexture(data, width, height);
}

void RendererDX11::DestroyTexture(ImTextureID texture)
{
    return ImGui_DestroyTexture(texture);
}

int RendererDX11::GetTextureWidth(ImTextureID texture)
{
    return ImGui_GetTextureWidth(texture);
}

int RendererDX11::GetTextureHeight(ImTextureID texture)
{
    return ImGui_GetTextureHeight(texture);
}

static void GpuFinish(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    ID3D11Query* query = nullptr;
    D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
    device->CreateQuery(&qd, &query);
    ctx->End(query);
    while (ctx->GetData(query, nullptr, 0, 0) == S_FALSE) { /* spin */ }
    query->Release();
}

bool RendererDX11::TakeScreenshot(const char* filename)
{
    // Generate filename if not provided
    std::string fname;
    if (!filename)
    {
        time_t now = time(nullptr);
        char buffer[64];
        strftime(buffer, sizeof(buffer), "screenshot_%Y%m%d_%H%M%S.png", localtime(&now));
        fname = buffer;
    }
    else
        fname = filename;

    // Flush any pending GPU work before capturing
    m_deviceContext->Flush();

    // Get back buffer
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr) || !backBuffer) 
        return false;

    // Get description
    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);
    
    // Create staging texture for CPU readback
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    
    ID3D11Texture2D* stagingTexture = nullptr;
    hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr) || !stagingTexture)
    {
        backBuffer->Release();
        return false;
    }

    // Copy back buffer to staging
    m_deviceContext->CopyResource(stagingTexture, backBuffer);
    m_deviceContext->Flush();
    
    // Wait for GPU to finish
    GpuFinish(m_device, m_deviceContext);
    
    backBuffer->Release();

    // Map and read
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_deviceContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
    {
        stagingTexture->Release();
        return false;
    }

    // Check format
    const bool isBGRA = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                         desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    const bool isRGBA = (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                         desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    if (!isBGRA && !isRGBA)
    {
        m_deviceContext->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        return false;
    }

    const int w = (int)desc.Width;
    const int h = (int)desc.Height;
    std::vector<unsigned char> rgba(w * h * 4);

    // Copy row by row and convert BGRA to RGBA if needed
    // DirectX11 render targets are typically already in the correct orientation (top-to-bottom)
    for (int y = 0; y < h; ++y)
    {
        const unsigned char* srcRow = (const unsigned char*)mapped.pData + y * mapped.RowPitch;
        unsigned char* dstRow = rgba.data() + y * w * 4;
        
        for (int x = 0; x < w; ++x)
        {
            const unsigned char* p = srcRow + x * 4;
            // DXGI_FORMAT_R8G8B8A8_UNORM is actually BGRA in memory!
            // Convert BGRA to RGBA
            dstRow[x * 4 + 0] = p[2]; // R <- B
            dstRow[x * 4 + 1] = p[1]; // G <- G
            dstRow[x * 4 + 2] = p[0]; // B <- R
            dstRow[x * 4 + 3] = p[3]; // A <- A
        }
    }

    m_deviceContext->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    // Write PNG
    int ok = stbi_write_png(fname.c_str(), w, h, 4, rgba.data(), w * 4);
    return ok != 0;
}

# endif // RENDERER(IMGUI_DX11)