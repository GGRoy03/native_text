// [Inputs/Outputs]

cbuffer Constants : register(b0)
{
    float3x3 Transform;
    float2   ViewportSizeInPixel;
    float2   AtlasSizeInPixel;
};

struct CPUToVertex
{
    float4 RectInPixel     : POS;
    float4 AtlasSrcInPixel : FONT;
    float4 Color           : COL;
    uint   VertexId        : SV_VertexID;
};

struct VertexToPixel
{
    float4 Position          : SV_POSITION;
    float4 Color             : COL;
    float2 TexCoordInPercent : TXP;
};

Texture2D    AtlasTexture : register(t0);
SamplerState AtlasSampler : register(s0);

// -------------------------------------------------------------

VertexToPixel VertexMain(CPUToVertex Input)
{
    float2 RectTopLeftInPixel   = Input.RectInPixel.xy;
    float2 RectBotRightInPixel  = Input.RectInPixel.zw;
    float2 AtlasTopLeftInPixel  = Input.AtlasSrcInPixel.xy;
    float2 AtlasBotRightInPixel = Input.AtlasSrcInPixel.zw;

    float2 CornerPositionInPixel[] =
    {
        float2(RectTopLeftInPixel.x , RectBotRightInPixel.y),
        float2(RectTopLeftInPixel.x , RectTopLeftInPixel.y ),
        float2(RectBotRightInPixel.x, RectBotRightInPixel.y),
        float2(RectBotRightInPixel.x, RectTopLeftInPixel.y ),
    };

    float2 AtlasSourceInPixel[] =
    {
        float2(AtlasTopLeftInPixel.x , AtlasBotRightInPixel.y),
        float2(AtlasTopLeftInPixel.x , AtlasTopLeftInPixel.y ),
        float2(AtlasBotRightInPixel.x, AtlasBotRightInPixel.y),
        float2(AtlasBotRightInPixel.x, AtlasTopLeftInPixel.y ),
    };

    float2 Transformed = mul(Transform, float3(CornerPositionInPixel[Input.VertexId], 1.f)).xy;
    Transformed.y = ViewportSizeInPixel.y - Transformed.y;

    VertexToPixel Output;
    Output.Position.xy = ((2.f * Transformed) / ViewportSizeInPixel) - 1.f;
    Output.Position.z  = 0.f;
    Output.Position.w  = 1.f;

    Output.Color             = Input.Color;
    Output.TexCoordInPercent = AtlasSourceInPixel[Input.VertexId] / AtlasSizeInPixel;

    return Output;
}

// -------------------------------------------------------------

float4 PixelMain(VertexToPixel Input) : SV_TARGET
{
    float4 Sample = AtlasTexture.Sample(AtlasSampler, Input.TexCoordInPercent);
    Sample *= Input.Color;

    return Sample;
}
