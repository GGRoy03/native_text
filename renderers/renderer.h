#pragma once

class IRenderer
{
public:
    virtual void Init     (void *WindowHandle, int Width, int Height) = 0;
    virtual void Clear    (float R, float G, float B, float A)        = 0;
    virtual void Present  ()                                          = 0;
};
