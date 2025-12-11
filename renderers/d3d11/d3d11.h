#include "../renderer.h"

#define D3D11_NO_HELPERS
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>
#include "./d3d11_ps.h"
#include "./d3d11_vs.h"
#pragma comment (lib, "d3d11")
#pragma comment (lib, "dxgi")
#pragma comment (lib, "dxguid")
#pragma comment (lib, "d3dcompiler")

class d3d11_renderer : public IRenderer
{

public:
    void Init            (void *WindowHandle, int Width, int Height)  override;
    void Clear           (float R, float G, float B, float A)         override;
    void Present         ()                                           override;
    void UpdateTextCache (const ntext::rasterized_glyph_list &List)  override;

private:
    ID3D11Device           *Device;
    ID3D11DeviceContext    *DeviceContext;
    IDXGISwapChain1        *SwapChain;
    ID3D11RenderTargetView *RenderView;
    ID3D11BlendState       *DefaultBlendState;
    ID3D11SamplerState     *AtlasSamplerState;
    ID3D11RasterizerState  *RasterState;
    ID3D11VertexShader     *VtxShader;
    ID3D11PixelShader      *PxlShader;

    // Text Stuff
    ID3D11Texture2D                *AtlasTexture;
    ID3D11ShaderResourceView       *AtlasSRV;
};

void d3d11_renderer::Init(void *WindowHandle, int Width, int Height)
{
    this->Device           = nullptr;
    this->DeviceContext    = nullptr;
    this->SwapChain        = nullptr;
    this->RenderView       = nullptr;
    this->DefaultBlendState= nullptr;
    this->AtlasSamplerState= nullptr;
    this->RasterState      = nullptr;
    this->VtxShader        = nullptr;
    this->PxlShader        = nullptr;
    this->AtlasTexture     = nullptr;
    this->AtlasSRV         = nullptr;

    UINT              CreateFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL Levels[]    = { D3D_FEATURE_LEVEL_11_0 };
    D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, CreateFlags, Levels, ARRAYSIZE(Levels),
                      D3D11_SDK_VERSION, &this->Device, NULL, &this->DeviceContext);
    ASSERT(this->DeviceContext);

    IDXGIDevice *DXGIDevice = nullptr;
    this->Device->QueryInterface(__uuidof(IDXGIDevice), (void **)&DXGIDevice);
    if (DXGIDevice)
    {
        IDXGIAdapter *DXGIAdapter = nullptr;
        DXGIDevice->GetAdapter(&DXGIAdapter);
        if (DXGIAdapter)
        {
            IDXGIFactory2 *Factory = nullptr;
            DXGIAdapter->GetParent(__uuidof(IDXGIFactory2), (void **)&Factory);
            if (Factory)
            {
                DXGI_SWAP_CHAIN_DESC1 Desc = { 0 };
                Desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
                Desc.SampleDesc.Count = 1;
                Desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                Desc.BufferCount      = 2;
                Desc.Scaling          = DXGI_SCALING_NONE;
                Desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

                Factory->CreateSwapChainForHwnd((IUnknown *)this->Device, (HWND)WindowHandle, &Desc, 0, 0, &this->SwapChain);
                Factory->MakeWindowAssociation((HWND)WindowHandle, DXGI_MWA_NO_ALT_ENTER);
                Factory->Release();
            }

            DXGIAdapter->Release();
        }

        DXGIDevice->Release();
    }

    ASSERT(this->SwapChain);

    this->Device->CreateVertexShader(D3D11VtxShaderBytes, sizeof(D3D11VtxShaderBytes), 0, &this->VtxShader);
    this->Device->CreatePixelShader(D3D11PxlShaderBytes, sizeof(D3D11PxlShaderBytes), 0, &this->PxlShader);

    ASSERT(this->VtxShader);
    ASSERT(this->PxlShader);

    ID3D11Texture2D *BackBuffer = nullptr;
    this->SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&BackBuffer);
    if(BackBuffer)
    {
        this->Device->CreateRenderTargetView((ID3D11Resource*)BackBuffer, NULL, &this->RenderView);

        BackBuffer->Release();
    }

    ASSERT(this->RenderView);

    D3D11_BLEND_DESC BlendDesc = {};
    BlendDesc.RenderTarget[0].BlendEnable           = TRUE;
    BlendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
    BlendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    BlendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    BlendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    BlendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
    BlendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    this->Device->CreateBlendState(&BlendDesc, &this->DefaultBlendState);
    ASSERT(this->DefaultBlendState);

    D3D11_SAMPLER_DESC SamplerDesc = {};
    SamplerDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
    SamplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.MaxAnisotropy  = 1;
    SamplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    SamplerDesc.MaxLOD         = D3D11_FLOAT32_MAX;

    this->Device->CreateSamplerState(&SamplerDesc, &this->AtlasSamplerState);
    ASSERT(this->AtlasSamplerState);

    D3D11_RASTERIZER_DESC RasterizerDesc = {};
    RasterizerDesc.FillMode              = D3D11_FILL_SOLID;
    RasterizerDesc.CullMode              = D3D11_CULL_BACK;
    RasterizerDesc.FrontCounterClockwise = FALSE;
    RasterizerDesc.DepthClipEnable       = TRUE;
    RasterizerDesc.ScissorEnable         = TRUE; 
    RasterizerDesc.MultisampleEnable     = FALSE;
    RasterizerDesc.AntialiasedLineEnable = FALSE;

    this->Device->CreateRasterizerState(&RasterizerDesc, &this->RasterState);
    ASSERT(this->RasterState);

    {
        D3D11_TEXTURE2D_DESC AtlasDesc = {};
        AtlasDesc.Width          = 2048; // atlas size - change as you need
        AtlasDesc.Height         = 2048;
        AtlasDesc.MipLevels      = 1;
        AtlasDesc.ArraySize      = 1;
        AtlasDesc.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
        AtlasDesc.SampleDesc.Count = 1;
        AtlasDesc.Usage          = D3D11_USAGE_DYNAMIC;                 // so CPU can map it
        AtlasDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;         // shaders can sample it
        AtlasDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;             // allow Map for writing
        AtlasDesc.MiscFlags      = 0;

        HRESULT hr = this->Device->CreateTexture2D(&AtlasDesc, nullptr, &this->AtlasTexture);
        ASSERT(SUCCEEDED(hr) && this->AtlasTexture);

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = AtlasDesc.Format;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels = 1;

        hr = this->Device->CreateShaderResourceView(this->AtlasTexture, &SRVDesc, &this->AtlasSRV);
        ASSERT(SUCCEEDED(hr) && this->AtlasSRV);
    }
}

void d3d11_renderer::Clear(float R, float G, float B, float A)
{
    float Color[4] = { R, G, B, A };
    this->DeviceContext->ClearRenderTargetView(this->RenderView, Color);
}

void d3d11_renderer::UpdateTextCache(const ntext::rasterized_glyph_list &List)
{
    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    HRESULT HR = this->DeviceContext->Map(this->AtlasTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
    ASSERT(SUCCEEDED(HR) && Mapped.pData);

    // Atlas is forced to RGBA8 for now.
    uint32_t AtlasBPP      = 4;
    uint8_t *AtlasBase     = (uint8_t *)Mapped.pData;
    uint32_t AtlasRowPitch = (uint32_t)Mapped.RowPitch;

    for (ntext::rasterized_glyph_node *Node = List.First; Node != 0; Node = Node->Next)
    {
        ntext::rasterized_glyph  &Glyph  = Node->Value;
        ntext::rasterized_buffer &Buffer = Glyph.Buffer;

        // assume alpha-only source
        ASSERT(Buffer.BytesPerPixel == 1);

        // rectangle -> destination region inside atlas
        uint32_t DstLeft   = (uint32_t)Glyph.Source.Left;
        uint32_t DstTop    = (uint32_t)Glyph.Source.Top;
        uint32_t DstRight  = (uint32_t)Glyph.Source.Right;
        uint32_t DstBottom = (uint32_t)Glyph.Source.Bottom;

        uint32_t CopyWidth  = (DstRight  > DstLeft) ? (DstRight - DstLeft) : 0;
        uint32_t CopyHeight = (DstBottom > DstTop)  ? (DstBottom - DstTop) : 0;
        if (CopyWidth == 0 || CopyHeight == 0) continue;

        // Assert the glyph rectangle fits inside mapped texture memory (caller guarantee)
        // We can't get exact atlas width/height from Mapped, but assume caller ensures this.
        ASSERT(AtlasRowPitch >= CopyWidth * AtlasBPP);

        uint8_t *SrcBase   = (uint8_t *)Buffer.Data;
        uint32_t SrcStride = Buffer.Stride ? Buffer.Stride : CopyWidth;

        for (uint32_t y = 0; y < CopyHeight; ++y)
        {
            uint8_t *SrcRow = SrcBase   + (size_t)y * SrcStride;
            uint8_t *DstRow = AtlasBase + (size_t)(DstTop + y) * AtlasRowPitch + (size_t)DstLeft * AtlasBPP;

            for (uint32_t x = 0; x < CopyWidth; ++x)
            {
                uint8_t  Alpha    = SrcRow[x];
                uint8_t *DstPixel = DstRow + (size_t)x * AtlasBPP;

                DstPixel[0] = 0xFF;
                DstPixel[1] = 0xFF;
                DstPixel[2] = 0xFF;
                DstPixel[3] = Alpha;
            }
        }
    }

    this->DeviceContext->Unmap(this->AtlasTexture, 0);
}

void d3d11_renderer::Present()
{
    this->SwapChain->Present(1, 0);
}
