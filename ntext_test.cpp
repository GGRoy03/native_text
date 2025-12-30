#include "src/ntext.h"
#include "renderers/renderer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <imm.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "d2d1")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "GDI32.lib")

static LRESULT CALLBACK
Win32Proc(HWND Handle, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch(Message)
    {

    case WM_CLOSE:
    {
        DestroyWindow(Handle);
        return 0;
    } break;

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    } break;

    }

    return DefWindowProc(Handle, Message, WParam, LParam);
}

static HWND
Win32Init(float Width, float Height)
{
    WNDCLASSEX WindowClass = { 0 };
    WindowClass.cbSize        = sizeof(WNDCLASSEX);
    WindowClass.style         = CS_HREDRAW | CS_VREDRAW;
    WindowClass.lpfnWndProc   = Win32Proc;
    WindowClass.hInstance     = GetModuleHandle(0);
    WindowClass.hIcon         = LoadIcon(0, IDI_APPLICATION);
    WindowClass.hCursor       = LoadCursor(0, IDC_ARROW);
    WindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
    WindowClass.lpszMenuName  = 0;
    WindowClass.lpszClassName = "Stub Window";
    WindowClass.hIconSm       = LoadIcon(0, IDI_APPLICATION);

    if(!RegisterClassEx(&WindowClass))
    {
        MessageBox(0, "Error registering class", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    HWND Handle = CreateWindowEx(0, WindowClass.lpszClassName,
                                 "Native Text", WS_OVERLAPPEDWINDOW,
                                 0, 0, Width, Height,
                                 0, 0, WindowClass.hInstance, 0);
    ShowWindow(Handle, SW_SHOWNORMAL);

    return Handle;
}

static void
Win32Sleep(DWORD Time)
{
    Sleep(Time);
}

int main()
{
    HWND HWindow = Win32Init(1920, 1080);

    d3d11_renderer Renderer;
    Renderer.Init(HWindow, 1920, 1080);

    ntext::glyph_generator_params Params = {};
    {
        Params.FrameMemoryBudget = 10 * 1024 * 1024;
        Params.FrameMemory       = malloc(Params.FrameMemoryBudget);
        Params.TextStorage       = ntext::TextStorage::LazyAtlas;
        Params.CacheSizeX        = 1024;
        Params.CacheSizeY        = 1024;
    }

    ntext::backend_context Backend   = ntext::InitializeBackendContext();
    ntext::system_font     Font      = ntext::LoadSystemFont("Consolas", 16.f, 0, Backend);
    ntext::glyph_generator Generator = ntext::CreateGlyphGenerator(Params);

    ntext::TextAnalysis     Flags    = ntext::TextAnalysis::SkipComplexCheck;
    ntext::analysed_text    Analysed = ntext::AnalyzeText((char *)"Hello", sizeof("Hello") - 1, Flags, Generator);
    ntext::shaped_glyph_run Run      = ntext::FillAtlas(Analysed, Generator, Backend);

    
    // Use the newly rasterized list to copy into the buffer.
    Renderer.UpdateTextCache(Run.UpdateList);

    while(true)
    {
        MSG Message;
        while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
        {
            if(Message.message == WM_QUIT)
            {
                goto END;
            }

            TranslateMessage(&Message);
            DispatchMessage(&Message);
        }

        Renderer.Clear(0.f, 0.f, 0.f, 1.f);
        Renderer.DrawTextToScreen();
        Renderer.Present();

        Win32Sleep(5);
    }

END:
    return 0;
}
