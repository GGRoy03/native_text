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
    void Init             (void *WindowHandle, int Width, int Height)  override;
    void Clear            (float R, float G, float B, float A)         override;
    void Present          ()                                           override;
    void UpdateTextCache  (const ntext::rasterized_glyph_list &List)   override;
    void DrawTextToScreen (void)                                       override;

private:

    // Objects
    ID3D11Device             *Device;
    ID3D11DeviceContext      *DeviceContext;
    IDXGISwapChain1          *SwapChain;
    ID3D11RenderTargetView   *RenderView;
    ID3D11BlendState         *DefaultBlendState;
    ID3D11SamplerState       *AtlasSamplerState;
    ID3D11RasterizerState    *RasterState;
    D3D11_VIEWPORT            Viewport;

    // Text Stuff
    ID3D11VertexShader       *VtxShader;
    ID3D11PixelShader        *PxlShader;
    ID3D11Texture2D          *AtlasTexture;
    ID3D11ShaderResourceView *AtlasSRV;
    ID3D11Buffer             *ConstantBuffer;
    ID3D11InputLayout        *InputLayout;
    ID3D11Buffer             *VertexBuffer;

    // State
    float                    ViewportWidth;
    float                    ViewportHeight;
    float                    AtlasWidth;
    float                    AtlasHeight;

    struct constant_buffer
    {
        float TransformRow0[4];
        float TransformRow1[4];
        float TransformRow2[4];
        float ViewportSize[2];
        float AtlasSize[2];
    };

    struct glyph_vertex
    {
        ntext::rectangle Bounds;
        ntext::rectangle Source;
        float            R, G, B, A;
    };
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
    this->ConstantBuffer  = nullptr;

    this->ViewportWidth    = (float)Width;
    this->ViewportHeight   = (float)Height;

    this->AtlasWidth       = 2048.0f;
    this->AtlasHeight      = 2048.0f;

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
    RasterizerDesc.CullMode              = D3D11_CULL_NONE;
    RasterizerDesc.FrontCounterClockwise = FALSE;
    RasterizerDesc.DepthClipEnable       = FALSE;
    RasterizerDesc.ScissorEnable         = FALSE; 
    RasterizerDesc.MultisampleEnable     = FALSE;
    RasterizerDesc.AntialiasedLineEnable = FALSE;

    this->Device->CreateRasterizerState(&RasterizerDesc, &this->RasterState);
    ASSERT(this->RasterState);

    {
        D3D11_TEXTURE2D_DESC AtlasDesc = {};
        AtlasDesc.Width          = this->AtlasWidth;
        AtlasDesc.Height         = this->AtlasHeight;
        AtlasDesc.MipLevels      = 1;
        AtlasDesc.ArraySize      = 1;
        AtlasDesc.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
        AtlasDesc.SampleDesc.Count = 1;
        AtlasDesc.Usage          = D3D11_USAGE_DYNAMIC;
        AtlasDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
        AtlasDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
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

    {
        D3D11_INPUT_ELEMENT_DESC LayoutDesc[] =
        {
            { "POS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // Bounds
            { "FONT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // Source
            { "COL",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 }, // Color (R,G,B,A)
        };

        HRESULT hr = this->Device->CreateInputLayout(LayoutDesc, ARRAYSIZE(LayoutDesc), D3D11VtxShaderBytes, sizeof(D3D11VtxShaderBytes), &this->InputLayout);

        ASSERT(SUCCEEDED(hr) && this->InputLayout);
    }

    {
        D3D11_BUFFER_DESC BufferDesc = {};
        BufferDesc.Usage          = D3D11_USAGE_DYNAMIC;
        BufferDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        BufferDesc.MiscFlags      = 0;
        BufferDesc.ByteWidth      = sizeof(constant_buffer);

        ASSERT((BufferDesc.ByteWidth % 16) == 0);

        HRESULT hr = this->Device->CreateBuffer(&BufferDesc, nullptr, &this->ConstantBuffer);
        ASSERT(SUCCEEDED(hr) && this->ConstantBuffer);
    }

    {
        size_t VertexBufferSize = 64 * 1024;

        D3D11_BUFFER_DESC BufferDesc = {};
        BufferDesc.Usage          = D3D11_USAGE_DYNAMIC;
        BufferDesc.ByteWidth      = (UINT)VertexBufferSize;
        BufferDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = this->Device->CreateBuffer(&BufferDesc, nullptr, &this->VertexBuffer);
        ASSERT(SUCCEEDED(hr) && this->VertexBuffer);
    }

    {
        this->Viewport.TopLeftX = 0.0f;
        this->Viewport.TopLeftY = 0.0f;
        this->Viewport.Width    = this->ViewportWidth;
        this->Viewport.Height   = this->ViewportHeight;
        this->Viewport.MinDepth = 0.0f;
        this->Viewport.MaxDepth = 1.0f;
    }
}

void d3d11_renderer::Clear(float R, float G, float B, float A)
{
    float Color[4] = { R, G, B, A };
    this->DeviceContext->ClearRenderTargetView(this->RenderView, Color);
}

// So this is bugged. Which is fine. We can (hopefully) easily debug it using RenderDocs.
// I haven't checked any of the code relating to this (or the buffer generation)
// Hopefully this is simpler than this stub implementation.

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

        // Assume alpha-only source for now.
        ASSERT(Buffer.BytesPerPixel == 1);

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

        for (uint32_t y = 0; y < Buffer.Height; ++y)
        {
            uint8_t *SrcRow = SrcBase   + (size_t)y * SrcStride;
            uint8_t *DstRow = AtlasBase + (size_t)(DstTop + y) * AtlasRowPitch + (size_t)DstLeft * AtlasBPP;

            for (uint32_t x = 0; x < Buffer.Width; ++x)
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

void d3d11_renderer::DrawTextToScreen(void)
{
    // Update Buffer

    // There is a problem because we expect floats in the shader.
    
    ntext::rectangle Bounds =
    {
        .Left   = 100.f,
        .Top    = 100.f,
        .Right  = 114.f,
        .Bottom = 118.f,
    };

    ntext::rectangle Source =
    {
        .Left   = 0.f,
        .Top    = 0.f,
        .Right  = 14.f,
        .Bottom = 18.f,
    };

    glyph_vertex Glyph =
    {
        .Bounds = Bounds,
        .Source = Source,
        .R      = 1.f,
        .G      = 0.f,
        .B      = 0.f,
        .A      = 1.f,
    };

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    HRESULT HR = DeviceContext->Map(this->VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
    if (SUCCEEDED(HR) && Mapped.pData)
    {
        memcpy(Mapped.pData, &Glyph, sizeof(Glyph));
        DeviceContext->Unmap(this->VertexBuffer, 0);
    }


    // Update Constants
    constant_buffer ConstantBuffer = {};
    ConstantBuffer.TransformRow0[0] = 1.0f; ConstantBuffer.TransformRow0[1] = 0.0f; ConstantBuffer.TransformRow0[2] = 0.0f; ConstantBuffer.TransformRow0[3] = 0.0f;
    ConstantBuffer.TransformRow1[0] = 0.0f; ConstantBuffer.TransformRow1[1] = 1.0f; ConstantBuffer.TransformRow1[2] = 0.0f; ConstantBuffer.TransformRow1[3] = 0.0f;
    ConstantBuffer.TransformRow2[0] = 0.0f; ConstantBuffer.TransformRow2[1] = 0.0f; ConstantBuffer.TransformRow2[2] = 1.0f; ConstantBuffer.TransformRow2[3] = 0.0f;

    ConstantBuffer.ViewportSize[0] = this->ViewportWidth;
    ConstantBuffer.ViewportSize[1] = this->ViewportHeight;

    ConstantBuffer.AtlasSize[0] = this->AtlasWidth;
    ConstantBuffer.AtlasSize[1] = this->AtlasHeight;

    D3D11_MAPPED_SUBRESOURCE MappedBuffer = {};
    HR = this->DeviceContext->Map(this->ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedBuffer);
    ASSERT(SUCCEEDED(HR) && MappedBuffer.pData);
    memcpy(MappedBuffer.pData, &ConstantBuffer, sizeof(ConstantBuffer));
    this->DeviceContext->Unmap(this->ConstantBuffer, 0);

    // Input Assembler State
    UINT Stride = sizeof(glyph_vertex);
    UINT Offset = 0;
    this->DeviceContext->IASetVertexBuffers(0, 1, &this->VertexBuffer, &Stride, &Offset);
    this->DeviceContext->IASetInputLayout(this->InputLayout);
    this->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // Output Merger State
    float BlendFactor[4] = {0,0,0,0};
    this->DeviceContext->OMSetBlendState(this->DefaultBlendState, BlendFactor, 0xffffffff);
    this->DeviceContext->OMSetRenderTargets(1, &this->RenderView, nullptr);

    // Vertex Shader State
    this->DeviceContext->VSSetShader(this->VtxShader, nullptr, 0);
    this->DeviceContext->VSSetConstantBuffers(0, 1, &this->ConstantBuffer);

    // Pixel Shader State
    this->DeviceContext->PSSetShader(this->PxlShader, nullptr, 0);
    this->DeviceContext->PSSetConstantBuffers(0, 1, &this->ConstantBuffer);
    this->DeviceContext->PSSetShaderResources(0, 1, &this->AtlasSRV);
    this->DeviceContext->PSSetSamplers(0, 1, &this->AtlasSamplerState);

    // Raster State
    this->DeviceContext->RSSetState(this->RasterState);
    this->DeviceContext->RSSetViewports(1, &this->Viewport);

    // Draw
    this->DeviceContext->DrawInstanced(4, 1, 0, 0);
}

void d3d11_renderer::Present()
{
    this->SwapChain->Present(1, 0);
}
