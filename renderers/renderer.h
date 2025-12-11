#pragma once

#define ASSERT(Cond) do {if (!(Cond)) __assume(0);} while (0)

class IRenderer
{

public:
    virtual void Init     (void *WindowHandle, int Width, int Height) = 0;
    virtual void Clear    (float R, float G, float B, float A)        = 0;
    virtual void Present  ()                                          = 0;

    virtual void UpdateTextCache  (const ntext::rasterized_glyph_list &List) = 0;
    virtual void DrawTextToScreen (void)                                     = 0;
};

#include "./d3d11/d3d11.h"
