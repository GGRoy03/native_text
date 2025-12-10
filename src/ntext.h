// ==================================================================================
// @Internal : Includes & Context Cracking
// Placeholder: include and platform detection section
// ==================================================================================

#include <immintrin.h>
#include <stdint.h>

#define NTEXT_ASSERT(Cond) do {if (!(Cond)) __debugbreak();} while (0)
#define NTEXT_ALIGNPOW2(x,b) (((x) + (b) - 1)&(~((b) - 1)))

#ifdef _WIN32
#define NTEXT_WIN32 1
#endif

#if defined(_MSV_VER)
    #define NTEXT_MSVC 1
#elif defined(__clang__)
    #define NTEXT_CLANG 1
#elif defined(__GNUC__)
    #define NTEXT_GNU 1
#else
    #error "Unknown Compiler"
#endif

#if NTEXT_MSVC

static inline unsigned FindFirstBit(uint32_t Mask)
{
    NTEXT_ASSERT(Mask != 0);
    return _tzcnt_u32(Mask);
}


#elif NTEXT_CLANG || NTEXT_GNU


static inline unsigned FindFirstBit(uint32_t Mask)
{
    NTEXT_ASSERT(Mask != 0);
    return __builtin_ctz(Mask);
}


#else
    #error "FindFirstBit not supported for this compiler."
#endif

#if NTEXT_MSVC || NTEXT_CLANG
    #define AlignOf(T) __alignof(T)
#elif NTEXT_GNU
    #define AlignOf(T) __alignof(T)__
#else
    #error "AlignOf not supported for this compiler"
#endif


#if NTEXT_WIN32
#include <windows.h>
#include <dwrite.h>

#include <wrl.h>
#include <vector>
#include <cassert>
#include <fstream>
#include <iostream>

#pragma comment(lib, "dwrite")
#endif

namespace ntext
{

// ==================================================================================
// @Internal : Various Utilities
// Placeholder: memory arena and helpers
// ==================================================================================

struct memory_arena
{
    uint64_t Reserved;
    uint64_t BasePosition;
    uint64_t Position;
};


struct memory_region
{
    memory_arena *Arena;
    uint64_t      Position;
};


static void *
PushArena(memory_arena *Arena, uint64_t Size, uint64_t Alignment)
{
    NTEXT_ASSERT(Arena);
    NTEXT_ASSERT(Size);
    NTEXT_ASSERT(Alignment);

    void *Result = 0;

    uint64_t PrePosition  = NTEXT_ALIGNPOW2(Arena->Position, Alignment);
    uint64_t PostPosition = PrePosition + Size;

    // NOTE:
    // We do not allow memory budget excess. Is that a mistake?
    // I mean I want to do this 0 allocations. Uhm. Tricky. Perhaps the user can
    // grow the memory budget on failure... Unsure.

    if(PostPosition <= Arena->Reserved)
    {
        Arena->Position = PostPosition;
        Result = ((uint8_t *)Arena + PrePosition);
    }

    return Result;
}


static void
ClearArena(memory_arena *Arena)
{
    Arena->Position = sizeof(memory_arena);
}


static uint64_t
GetArenaPosition(memory_arena *Arena)
{
    uint64_t Result = Arena->BasePosition + Arena->Position;
    return Result;
}


static memory_region
EnterMemoryRegion(memory_arena *Arena)
{
    memory_region Result;
    Result.Arena    = Arena;
    Result.Position = GetArenaPosition(Arena);

    return Result;
}


static void
LeaveMemoryRegion(memory_region Region)
{
    Region.Arena->Position = Region.Position;
}


#define PushArrayNoZeroAligned(a, T, c, align) (T *)PushArena((a), sizeof(T)*(c), (align))
#define PushArrayAligned(a, T, c, align) PushArrayNoZeroAligned(a, T, c, align)
#define PushArray(a, T, c) PushArrayAligned(a, T, c, max(8, AlignOf(T)))
#define PushStruct(a, T)   PushArrayAligned(a, T, 1, max(8, AlignOf(T)))


// ==================================================================================
// @Internal : Win32 Implementation
// Placeholder: DirectWrite integration & rasterization helpers
// ==================================================================================

#ifdef NTEXT_WIN32

static bool WriteGrayscaleBMP(const wchar_t *path, int w, int h, BYTE *Pixels)
{
    if (w <= 0 || h <= 0) return false;

    const int rowBytesUnpadded = w;
    const int rowBytes = (rowBytesUnpadded + 3) & ~3;
    const int imageSize = rowBytes * h;
    const int fileHeaderSize = 14;
    const int infoHeaderSize = 40;
    const int paletteBytes = 256 * 4;
    const int offsetToPixels = fileHeaderSize + infoHeaderSize + paletteBytes;
    const int fileSize = offsetToPixels + imageSize;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.put('B'); f.put('M');
    f.write((const char *)&fileSize, 4);
    int reserved = 0;
    f.write((const char *)&reserved, 4);
    f.write((const char *)&offsetToPixels, 4);

    int infoSize = infoHeaderSize;
    f.write((const char *)&infoSize, 4);
    f.write((const char *)&w, 4);
    f.write((const char *)&h, 4);
    short planes = 1;
    f.write((const char *)&planes, 2);
    short bitcount = 8;
    f.write((const char *)&bitcount, 2);
    int compression = 0;
    f.write((const char *)&compression, 4);
    f.write((const char *)&imageSize, 4);
    int ppm = 2835;
    f.write((const char *)&ppm, 4);
    f.write((const char *)&ppm, 4);
    int clrUsed = 256;
    f.write((const char *)&clrUsed, 4);
    int clrImportant = 256;
    f.write((const char *)&clrImportant, 4);

    for (int i = 0; i < 256; ++i) {
        unsigned char b = (unsigned char)i;
        unsigned char g = (unsigned char)i;
        unsigned char r = (unsigned char)i;
        unsigned char a = 0;
        f.put(b); f.put(g); f.put(r); f.put(a);
    }

    std::vector<BYTE> pad(rowBytes - rowBytesUnpadded);
    for (int y = h - 1; y >= 0; --y) {
        const BYTE *rowSrc = Pixels + y * w;
        f.write((const char *)rowSrc, rowBytesUnpadded);
        if (!pad.empty()) f.write((const char *)pad.data(), (std::streamsize)pad.size());
    }
    f.close();
    return true;
}


struct os_glyph_info
{
    uint16_t GlyphIndex;

    float Advance;
    float OffsetX;
    float OffsetY;

    float SizeX;
    float SizeY;
};

struct rasterized_buffer
{
    void    *Buffer;
    uint32_t Stride;
    uint32_t BufferWidth;
    uint32_t BufferHeight;
    uint32_t BytesPerPixel;
};


struct backend_context
{
    bool              IsValid                       ();

    os_glyph_info     FindGlyphInformation          (uint32_t CodePointer, float FontSize);
    rasterized_buffer RasterizeGlyphToAlphaTexture  (uint16_t GlyphIndex, float Advance, float EmSize, memory_arena *Arena);

    IDWriteFactory  *DirectWrite;
    IDWriteFontFace *Font;
};


static backend_context CreateBackendContext(void)
{
    backend_context Context = {};

    IDWriteFactory *DirectWrite;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&DirectWrite));

    NTEXT_ASSERT(DirectWrite);

    IDWriteFontCollection *Fonts;
    DirectWrite->GetSystemFontCollection(&Fonts);
    NTEXT_ASSERT(Fonts);

    UINT32 FamilyIndex = 0;
    BOOL   Exists      = FALSE;
    const wchar_t *FamilyToFind = L"Segoe UI";
    Fonts->FindFamilyName(FamilyToFind, &FamilyIndex, &Exists);
    NTEXT_ASSERT(Exists);

    IDWriteFontFamily *Family;
    Fonts->GetFontFamily(FamilyIndex, &Family);
    NTEXT_ASSERT(Family);

    IDWriteFont *Font;
    Family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STRETCH_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL,
                                &Font);
    NTEXT_ASSERT(Font);

    IDWriteFontFace *FontFace;
    Font->CreateFontFace(&FontFace);
    NTEXT_ASSERT(Font);

    Context.DirectWrite = DirectWrite;
    Context.Font        = FontFace;

    return Context;
}


bool backend_context::IsValid()
{
    bool Result = (this->DirectWrite) && (this->Font);
    return Result;
}


os_glyph_info
backend_context::FindGlyphInformation(uint32_t CodePoint, float EmSize)
{
    HRESULT Hr = S_OK;

    IDWriteFontFace *FontFace = this->Font;
    IDWriteFactory  *DWrite   = this->DirectWrite;

    UINT16 GlyphIndex = 0;
    Hr = FontFace->GetGlyphIndices(&CodePoint, 1, &GlyphIndex);

    DWRITE_GLYPH_METRICS GlyphMetrics = {};
    Hr = FontFace->GetDesignGlyphMetrics(&GlyphIndex, 1, &GlyphMetrics, FALSE);

    DWRITE_FONT_METRICS FontMetrics = {};
    FontFace->GetMetrics(&FontMetrics);

    float Scale = (FontMetrics.designUnitsPerEm > 0) ? (EmSize / (float)FontMetrics.designUnitsPerEm) : 1.0f;

    int32_t  Left    = GlyphMetrics.leftSideBearing;
    uint32_t Advance = GlyphMetrics.advanceWidth;
    int32_t  Right   = GlyphMetrics.rightSideBearing;
    int32_t  Top     = GlyphMetrics.topSideBearing;
    uint32_t AHeight = GlyphMetrics.advanceHeight;
    int32_t  Bottom  = GlyphMetrics.bottomSideBearing;

    os_glyph_info Result =
    {
        .GlyphIndex = GlyphIndex,
        .Advance    = static_cast<float>(Advance * Scale),
        .OffsetX    = static_cast<float>(Left    * Scale),
        .OffsetY    = static_cast<float>(Top     * Scale),
        .SizeX      = static_cast<float>((Left   + Advance + Right)  * Scale),
        .SizeY      = static_cast<float>((Top    + AHeight + Bottom) * Scale),
    };

    return Result;
}

rasterized_buffer
backend_context::RasterizeGlyphToAlphaTexture(uint16_t GlyphIndex, float Advance, float EmSize, memory_arena *Arena)
{
    rasterized_buffer Result = {};

    // Need to check for NULL here? Maybe assert since it probably needs to be done before this point.

    IDWriteFontFace *FontFace = this->Font;
    IDWriteFactory  *DWrite   = this->DirectWrite;

    UINT16              GlyphIndices[1] = {GlyphIndex};
    FLOAT               Advances[1]     = {Advance};
    DWRITE_GLYPH_OFFSET Offsets[1]      = {0.f, 0.f};

    DWRITE_GLYPH_RUN GlyphRun =
    {
        .fontFace      = FontFace,
        .fontEmSize    = EmSize,
        .glyphCount    = 1,
        .glyphIndices  = GlyphIndices,
        .glyphAdvances = Advances,
        .glyphOffsets  = Offsets,
        .isSideways    = FALSE,
        .bidiLevel     = 0,
    };

    IDWriteGlyphRunAnalysis *RunAnalysis = 0;
    DWrite->CreateGlyphRunAnalysis(&GlyphRun, 1.f, 0,
                                   DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
                                   0.f, 0.f, &RunAnalysis);

    if(RunAnalysis)
    {
        RECT TextureBounds = {};
        RunAnalysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &TextureBounds);

        int TextureWidth  = TextureBounds.right  - TextureBounds.left;
        int TextureHeight = TextureBounds.bottom - TextureBounds.top;

        if(TextureWidth > 0 && TextureHeight > 0)
        {
            int      BytesPerPixel = 1;
            int      Stride        = TextureWidth * BytesPerPixel;
            uint32_t BufferSize    = TextureWidth * TextureHeight * BytesPerPixel;

            BYTE *Buffer = PushArray(Arena, BYTE, BufferSize);
            if(Buffer)
            {
                if(RunAnalysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &TextureBounds, Buffer, BufferSize) == S_OK)
                {
                    Result.Buffer        = Buffer;
                    Result.Stride        = Stride;
                    Result.BufferWidth   = TextureWidth;
                    Result.BufferHeight  = TextureHeight;
                    Result.BytesPerPixel = BytesPerPixel;
                }
            }

            // static int Counter = 0;
            // wchar_t OutPath[64];
            // swprintf(OutPath, 64, L"glyph_alpha_%d.bmp", Counter++);
            // WriteGrayscaleBMP(OutPath, TextureWidth, TextureHeight, Buffer);
        }

        RunAnalysis->Release();
    }

    return Result;
}


#endif // NTEXT_WIN32


// ==================================================================================
// @Internal : Rectangle Packing
// Placeholder: rectangle packing utilities
// ==================================================================================

struct point
{
    uint16_t X;
    uint16_t Y;
};


struct rectangle_packer
{
    point    *Skyline;
    uint16_t  SkylineCount;
    uint16_t  Width;
    uint16_t  Height;
};


struct packed_rectangle
{
    uint16_t Width;
    uint16_t Height;

    uint16_t X;
    uint16_t Y;
    bool     WasPacked;
};


struct rectangle
{
    uint16_t Left;
    uint16_t Top;
    uint16_t Right;
    uint16_t Bottom;
};


static uint64_t
GetRectanglePackerFootprint(uint16_t Width)
{
    uint64_t SkylineSize = Width * sizeof(point);
    uint64_t Result      = sizeof(rectangle_packer) + SkylineSize;

    return Result;
}


static rectangle_packer *
PlaceRectanglePackerInMemory(uint16_t Width, uint16_t Height, void *Memory)
{
    rectangle_packer *Result = 0;

    if(Memory)
    {
        point *Skyline = (point *)Memory;

        Result = (rectangle_packer *)(Skyline + Width);
        Result->Width        = Width;
        Result->Height       = Height;
        Result->Skyline      = Skyline;
        Result->SkylineCount = 1;
        Result->Skyline[0].X = 0;
        Result->Skyline[0].Y = 0;
    }

    return Result;
}


static void
PackRectangle(packed_rectangle &Rectangle, rectangle_packer *Packer)
{
    NTEXT_ASSERT(Packer);

    uint16_t Width  = Rectangle.Width;
    uint16_t Height = Rectangle.Height;

    if(Width == 0 || Height == 0)
    {
        return;
    }

    uint16_t BestIndexInclusive = UINT16_MAX;
    uint16_t BestIndexExclusive = UINT16_MAX;
    point    BestPoint          = {UINT16_MAX, UINT16_MAX};

    NTEXT_ASSERT(Packer->SkylineCount);

    for(uint16_t Idx = 0; Idx < Packer->SkylineCount; ++Idx)
    {
        point Point = Packer->Skyline[Idx];

        // If on the current skyline we do not have horizontal space, then we can break since the array is sorted left to right.
        if(Width > Packer->Width - Point.X)
        {
            break;
        }

        // If the current skyline is taller than our BestY, then we simply continue since we are trying to place bottom->top
        if(Point.Y >= BestPoint.Y)
        {
            continue;
        }

        uint16_t XMax           = Point.X + Width;
        uint16_t IndexExclusive = 0;
        for(IndexExclusive = Idx + 1; IndexExclusive < Packer->SkylineCount; ++IndexExclusive)
        {
            // XMax is the right edge of the current rectangle. If it is smaller or equal then we know that
            // there is no way to intersect on the X axis anymore, because the skylines are sorted from left to right.
            if(XMax <= Packer->Skyline[IndexExclusive].X)
            {
                break;
            }

            // At this point, we know we are intersecting on the X axis, thus we need to raise the skyline,
            // which is what creates dead space. As long as we intersect on the X axis, we have to continue raising the
            // point.
            if(Point.Y < Packer->Skyline[IndexExclusive].Y)
            {
                Point.Y = Packer->Skyline[IndexExclusive].Y;
            }
        }

        // Then it's a worse point than our current best.
        if(Point.Y >= BestPoint.Y)
        {
            continue;
        }

        // Not enough vertical space to store this rectangle.
        if(Height > Packer->Height - Point.Y)
        {
            continue;
        }

        BestIndexInclusive = Idx;
        BestIndexExclusive = IndexExclusive;
        BestPoint          = Point;
    }

    // Could not pack the rectangle.
    if(BestIndexInclusive == UINT16_MAX)
    {
        return;
    }

    NTEXT_ASSERT(BestIndexInclusive < BestIndexExclusive);
    NTEXT_ASSERT(BestIndexExclusive > 0);

    uint16_t RemovedCount = BestIndexExclusive - BestIndexInclusive;
    NTEXT_ASSERT(RemovedCount > 0);

    // To create the top left point, we use the best point we found and raise it by the height of the rectangle.
    // To create the bot right point, we use the right edge of the triangle we are placing and the last overlapping skyline y
    // coordinate. This ensures that the skyline goes from left to right.

    point NewTopLeft  = {BestPoint.X                               , static_cast<uint16_t>(BestPoint.Y + Height)};
    point NewBotRight = {static_cast<uint16_t>(BestPoint.X + Width), Packer->Skyline[BestIndexExclusive - 1].Y};

    // To know if we are creating a new skyline from the bottom right point, we need to check multiple cases.
    // If the first exclusive point is a valid index, then we simply check: if (smaller) new point else no.
    // If the first exclusive point is not a valid index, then we simply check: if(not_on_boundary) new point

    bool HasBottomRightPoint = BestIndexExclusive < Packer->SkylineCount ?
        NewBotRight.X < Packer->Skyline[BestIndexExclusive].X :
        NewBotRight.X < Packer->Width;

    uint16_t InsertedCount = 1 + HasBottomRightPoint;
    NTEXT_ASSERT(Packer->SkylineCount + InsertedCount - RemovedCount <= Packer->Width);

    if(InsertedCount > RemovedCount)
    {
        // Copy from the last element to the exclusive index.
        // Make space for the new elements by shifting end by (InsertedCount - RemovedCount)
        // This grows the array.

        uint16_t Start = Packer->SkylineCount - 1;
        uint16_t End   = Start + (InsertedCount - RemovedCount);

        for(; Start >= BestIndexExclusive; --Start, --End)
        {
            Packer->Skyline[End] = Packer->Skyline[Start];
        }

        Packer->SkylineCount += (InsertedCount - RemovedCount);
    } else
    if(InsertedCount < RemovedCount)
    {
        // Copy from the first exclusive element to (Start - ElementsToRemoveCount)
        // We basically start copying behind us and iterate until the end of the array.

        uint16_t Start = BestIndexExclusive;
        uint16_t End   = Start - (RemovedCount - InsertedCount);

        for(; Start < Packer->SkylineCount; ++Start, ++End)
        {
            Packer->Skyline[End] = Packer->Skyline[Start];
        }

        Packer->SkylineCount += (RemovedCount - InsertedCount);
    }

    Packer->Skyline[BestIndexInclusive] = NewTopLeft;
    if(HasBottomRightPoint)
    {
        Packer->Skyline[BestIndexInclusive + 1] = NewBotRight;
    }

    Rectangle.WasPacked = 1;
    Rectangle.X         = NewTopLeft.X;
    Rectangle.Y         = NewTopLeft.Y;
}


// ==================================================================================
// @Public : NText Glyph Table Implementation
// Placeholder: glyph table types and hash utilities
// ==================================================================================

enum class GlyphTableWidth : uint64_t
{
    None     = 0,
    _128Bits = 16,
};


struct glyph_table_params
{
    GlyphTableWidth GroupWidth;
    uint64_t        GroupCount;
};


struct glyph_hash
{
    __m128i Value;
};


struct glyph_tag
{
    uint8_t Value;
};


struct glyph_layout_info
{
    float Advance;
    float OffsetX;
    float OffsetY;
};


struct glyph_entry
{
    glyph_hash        Hash;

    uint32_t          PrevLRU;
    uint32_t          NextLRU;
    uint32_t          NextFreeEntry;

    uint16_t          GlyphIndex;
    rectangle         Source;
    glyph_layout_info LayoutInfo;
    bool              IsRasterized;
};


struct glyph_table
{
    uint8_t     *Metadata;
    glyph_entry *Buckets;
    glyph_entry  Sentinel;

    uint64_t     GroupWidth;
    uint64_t     GroupCount;
    uint64_t     HashMask;
};


struct glyph_state
{
    uint64_t          Id;
    uint16_t          GlyphIndex;
    glyph_layout_info LayoutInfo;
    rectangle         Source;
    bool              IsRasterized;
};


constexpr uint32_t GlyphTableInvalidEntry = 0xFFFFFFFFu;
constexpr uint8_t  GlyphTableEmptyMask    = 1 << 6; 
constexpr uint8_t  GlyphTableDeadMask     = 1 << 7;
constexpr uint8_t  GlyphTableTagMask      = 0xFF & ~0x03;


static bool
IsValidGlyphTable(glyph_table *Table)
{
    bool Result = Table && Table->Metadata && Table->Buckets;
    return Result;
}


static glyph_entry *
GetGlyphEntry(uint64_t Index, glyph_table *Table)
{
    NTEXT_ASSERT(Table);
    NTEXT_ASSERT(Index < (Table->GroupCount * Table->GroupWidth));

    glyph_entry *Result = Table->Buckets + Index;
    return Result;
}


static uint32_t
GetFreeGlyphEntry(glyph_table *Table)
{
    glyph_entry Sentinel = Table->Sentinel;

    if(Sentinel.NextFreeEntry == GlyphTableInvalidEntry)
    {
        NTEXT_ASSERT(!"Not Implemented");
    }

    uint32_t Result = Sentinel.NextFreeEntry;
    NTEXT_ASSERT(Result != GlyphTableInvalidEntry);

    glyph_entry *Entry = GetGlyphEntry(Result, Table);
    Sentinel.NextFreeEntry = Entry->NextFreeEntry;
    Entry->NextFreeEntry   = GlyphTableInvalidEntry;

    NTEXT_ASSERT(Entry);
    NTEXT_ASSERT(Entry->NextFreeEntry == GlyphTableInvalidEntry);
    NTEXT_ASSERT(Entry == GetGlyphEntry(Result, Table));

    return Result;
}


static uint32_t
GetGlyphGroupIndexFromHash(glyph_hash Hash, glyph_table *Table)
{
    uint64_t Low64  = _mm_cvtsi128_si64(Hash.Value);
    uint32_t Result = (uint32_t)(Low64 & Table->HashMask);

    return Result;
}


static char unsigned OverhangMask[32] =
{
    255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,
    0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0
};


static char unsigned DefaultSeed[16] =
{
    178, 201, 95, 240, 40, 41, 143, 216,
    2, 209, 178, 114, 232, 4, 176, 188
};


static glyph_hash
ComputeGlyphHash(size_t Count, char unsigned *At, char unsigned *Seedx16)
{
    glyph_hash Result = {0};

    __m128i HashValue = _mm_cvtsi64_si128(Count);
    HashValue = _mm_xor_si128(HashValue, _mm_loadu_si128((__m128i *)Seedx16));

    size_t ChunkCount = Count / 16;
    while(ChunkCount--)
    {
        __m128i In = _mm_loadu_si128((__m128i *)At);
        At += 16;

        HashValue = _mm_xor_si128(HashValue, In);
        HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());
        HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());
        HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());
        HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());
    }

    size_t Overhang = Count % 16;

    // Think there is a fix for that on the refterm issues tab.

#if 0
    __m128i In = _mm_loadu_si128((__m128i *)At);
#else
    char Temp[16];
    __movsb((unsigned char *)Temp, At, Overhang);
    __m128i In = _mm_loadu_si128((__m128i *)Temp);
#endif
    In = _mm_and_si128(In, _mm_loadu_si128((__m128i *)(OverhangMask + 16 - Overhang)));

    HashValue = _mm_xor_si128(HashValue, In);
    HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());
    HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());
    HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());
    HashValue = _mm_aesdec_si128(HashValue, _mm_setzero_si128());

    Result.Value = HashValue;

    return Result;
}


static glyph_tag
GetGlyphTagFromHash(glyph_hash Hash)
{
    uint64_t  Low64  = _mm_cvtsi128_si64(Hash.Value);
    glyph_tag Result = {.Value = (uint8_t)(Low64 & 0x3F)};

    return Result;
}


static bool
GlyphHashesAreEqual(glyph_hash A, glyph_hash B)
{
    __m128i Compare = _mm_cmpeq_epi8(A.Value, B.Value);
    int     Mask    = _mm_movemask_epi8(Compare);

    return (Mask == 0xFFFF);
}


static uint64_t
GetGlyphTableFootprint(glyph_table_params Params)
{
    uint64_t SlotCount = Params.GroupCount * static_cast<uint32_t>(Params.GroupWidth);

    uint64_t MetadataSize = SlotCount * sizeof(uint8_t);
    uint64_t BucketsSize  = SlotCount * sizeof(glyph_entry);
    uint64_t Result       = MetadataSize + BucketsSize + sizeof(glyph_table_params);

    return Result;
}


static glyph_table *
PlaceGlyphTableInMemory(glyph_table_params Params, void *Memory)
{
    glyph_table *Result = 0;

    if(Memory)
    {
        uint64_t SlotCount = Params.GroupCount * static_cast<uint32_t>(Params.GroupWidth);

        uint8_t     *Metadata = static_cast<uint8_t *>(Memory);
        glyph_entry *Buckets  = reinterpret_cast<glyph_entry*>(Metadata + SlotCount);

        Result = reinterpret_cast<glyph_table *>(Buckets + SlotCount);
        Result->Metadata   = Metadata;
        Result->Buckets    = Buckets;
        Result->GroupWidth = static_cast<uint64_t>(Params.GroupWidth);
        Result->GroupCount = Params.GroupCount;
        Result->HashMask   = Params.GroupCount - 1; // This is only used to find the group index, not the slot index.

        for(uint32_t Idx = 0; Idx < SlotCount; ++Idx)
        {
            glyph_entry *Entry = GetGlyphEntry(Idx, Result);

            if(Idx + 1 < SlotCount)
            {
                Entry->NextFreeEntry = Idx + 1;
            }
            else
            {
                Entry->NextFreeEntry = GlyphTableInvalidEntry;
            }

            Entry->GlyphIndex   = 0;
            Entry->Source       = {};
            Entry->LayoutInfo   = {};
            Entry->IsRasterized = false;
        }

        for(uint32_t Idx = 0; Idx < SlotCount; ++Idx)
        {
            Result->Metadata[Idx] = GlyphTableEmptyMask;
        }

        Result->Sentinel.PrevLRU = GlyphTableInvalidEntry;
        Result->Sentinel.NextLRU = GlyphTableInvalidEntry;
    }

    return Result;
}


static glyph_state
FindGlyphEntryByHash(glyph_hash Hash, glyph_table *Table)
{
    NTEXT_ASSERT(Table);

    glyph_entry *Result     = 0;
    uint32_t     EntryIndex = 0;

    uint32_t ProbeCount = 0;
    uint32_t GroupIndex = GetGlyphGroupIndexFromHash(Hash, Table);

    NTEXT_ASSERT(GroupIndex < Table->GroupCount);

    while(true)
    {
        uint8_t  *Meta = Table->Metadata + (GroupIndex * Table->GroupWidth);
        glyph_tag Tag  = GetGlyphTagFromHash(Hash);

        __m128i MetaVector = _mm_loadu_si128((__m128i *)Meta);
        __m128i TagVector  = _mm_set1_epi8(Tag.Value);

        // Uses a 6 bit tags to search a matching tag through the meta-data vector.
        // If found compare hashes and return if they match.

        int TagMask = _mm_movemask_epi8(_mm_cmpeq_epi8(MetaVector, TagVector));
        while(TagMask)
        {
            EntryIndex = FindFirstBit(TagMask) + (GroupIndex * Table->GroupWidth);

            glyph_entry *Entry = GetGlyphEntry(EntryIndex, Table);
            if(GlyphHashesAreEqual(Hash, Entry->Hash))
            {
                Result = Entry;
                break;
            }

            TagMask &= TagMask - 1;
        }

        if(!Result)
        {
            __m128i EmptyVector = _mm_set1_epi8(GlyphTableEmptyMask);
            int     EmptyMask   = _mm_movemask_epi8(_mm_cmpeq_epi8(MetaVector, EmptyVector));

            if(!EmptyMask)
            {
                ProbeCount++;
                GroupIndex = (GroupIndex + (ProbeCount * ProbeCount)) & (Table->GroupCount - 1);
            }
            else
            {
                break;
            }
        }
    }

    if(Result)
    {
        // An existing entry was found, we simply pop it from the chain.

        glyph_entry *Prev = GetGlyphEntry(Result->PrevLRU, Table);
        glyph_entry *Next = GetGlyphEntry(Result->NextLRU, Table);

        Prev->NextLRU = Result->NextLRU;
        Next->PrevLRU = Result->PrevLRU;
    }
    else
    {
        // No existing entry was found, allocate a new one and link it into the chain.
        // We also need to update the metadata array by clearing all state bits (empty and dead)
        // and writing the tag.

        EntryIndex = GetFreeGlyphEntry(Table);
        NTEXT_ASSERT(EntryIndex != GlyphTableInvalidEntry);

        // Could the compiler optimize this into a single write? I believe the 0 write is necessary for the logic here.
        uint8_t *Meta = Table->Metadata + EntryIndex;
        *Meta = 0;                                  // Clear all bits.
        *Meta = (GetGlyphTagFromHash(Hash).Value);  // Set the tag meta-data to the low 6 bits

        Result = GetGlyphEntry(EntryIndex, Table);
        Result->Hash = Hash;
    }

    glyph_entry *Sentinel = &Table->Sentinel;
    Result->NextLRU = Sentinel->NextLRU;
    Result->PrevLRU = GlyphTableInvalidEntry;

    // The only case where this matters is when writing the first entry. Maybe a slightly better design would remove this branch.
    if(Sentinel->NextLRU != GlyphTableInvalidEntry)
    {
        glyph_entry *LastNext = GetGlyphEntry(Sentinel->NextLRU, Table);
        LastNext->PrevLRU = EntryIndex;
    }

    Sentinel->NextLRU = EntryIndex;

    glyph_state State = {};
    State.IsRasterized = Result->IsRasterized;
    State.Id           = EntryIndex;

    return State;
}


// ==================================================================================
// @Public : NText Context
// Placeholder: generator and context management
// ==================================================================================

enum class TextStorage
{
    None      = 0,
    LazyAtlas = 1,
};


struct glyph_generator_params
{
    TextStorage TextStorage;
    uint64_t    FrameMemoryBudget;
    void       *FrameMemory;
};


struct glyph_generator
{
    // Memory
    memory_arena     *Arena;

    // Systems
    glyph_table      *GlyphTable;
    rectangle_packer *Packer;

    // Misc
    TextStorage       TextStorage;
    backend_context   Backend;
};


static glyph_generator CreateGlyphGenerator(glyph_generator_params Params)
{
    glyph_generator Generator = {};

    if(Params.FrameMemoryBudget == 0 || Params.FrameMemory == 0 || Params.TextStorage == TextStorage::None)
    {
        return Generator;
    }

    // Arena Initialization
    {
        NTEXT_ASSERT(Params.FrameMemory);
        NTEXT_ASSERT(Params.FrameMemoryBudget);

        Generator.Arena = static_cast<memory_arena *>(Params.FrameMemory);
        Generator.Arena->Reserved     = Params.FrameMemoryBudget;
        Generator.Arena->BasePosition = 0;
        Generator.Arena->Position     = sizeof(memory_arena);
    }

    // Glyph Table
    {
        glyph_table_params Params =
        {
            .GroupWidth = GlyphTableWidth::_128Bits,
            .GroupCount = 64,
        };

        uint64_t Footprint = GetGlyphTableFootprint(Params);
        void    *Memory    = PushArena(Generator.Arena, Footprint, AlignOf(void *));

        Generator.GlyphTable = PlaceGlyphTableInMemory(Params, Memory);

        NTEXT_ASSERT(Generator.GlyphTable);
    }

    // Packer
    {
        uint16_t Width  = 1024;
        uint16_t Height = 1024;

        uint64_t Footprint = GetRectanglePackerFootprint(Width);
        void    *Memory    = PushArena(Generator.Arena, Footprint, AlignOf(void *));

        Generator.Packer = PlaceRectanglePackerInMemory(Width, Height, Memory);

        NTEXT_ASSERT(Generator.Packer);
    }

    // Constant Forwarding
    {
        NTEXT_ASSERT(Params.TextStorage != TextStorage::None);

        Generator.TextStorage = Params.TextStorage;
    }

    // Backend Initialization
    {
        Generator.Backend = CreateBackendContext();
    }

    return Generator;
}


static bool IsValidGlyphGenerator(const glyph_generator &GlyphGenerator)
{
    bool Result = (GlyphGenerator.Arena != 0);
    return Result;
}


// ==================================================================================
// @Public : NText Collection
// Placeholder: texture & atlas routines
// ==================================================================================

enum class TextureFormat
{
    None      = 0,
    RGBA      = 1,
    GreyScale = 2,
};


static uint64_t GetTextureFormatBytesPerPixel(TextureFormat Format)
{
    switch(Format)
    {

    case TextureFormat::None:
    {
       return 0;
    } break;

    case TextureFormat::GreyScale:
    {
        return 1;
    } break;

    case TextureFormat::RGBA:
    {
        return 4;
    } break;

    }
}


static void
FillAtlas(char *Data, uint64_t Count, glyph_generator &Generator)
{
    uint32_t Advance     = 16;
    __m128i  ComplexByte = _mm_set1_epi8(0x80);

    bool HasComplexCharacter = false;

    // TODO: Rewrite this loop in a better way, since the only goal is to early
    // exit if we find a complex character.

    uint32_t ParseCount = Count;
    char    *ParseData  = Data;

    while(ParseCount)
    {
        __m128i ContainsComplex = _mm_setzero_si128();

        while(ParseCount >= Advance)
        {
            __m128i Batch = _mm_loadu_si128((__m128i *) ParseData);
            __m128i TestX = _mm_and_si128(Batch, ComplexByte);

            // NOTE:
            // If we break early, is there a better instruction than a OR?
            // This is not great.

            ContainsComplex = _mm_or_si128(ContainsComplex, TestX);
            if(_mm_movemask_epi8(ContainsComplex))
            {
                break;
            }

            ParseCount -= Advance;
            ParseData  += Advance;
        }

        HasComplexCharacter = _mm_movemask_epi8(ContainsComplex);
        if(HasComplexCharacter)
        {
            break;
        }

        char Token = ParseData[0];
        if(Token < 0)
        {
            HasComplexCharacter = 1;
            break;
        }

        ParseData++;
        ParseCount--;
    }

    if(HasComplexCharacter)
    {
        NTEXT_ASSERT(!"Not Implemented");
    }
    else
    {
        for(uint32_t Idx = 0; Idx < Count; ++Idx)
        {
            glyph_hash  Hash  = ComputeGlyphHash(1, (char unsigned *)&Data[Idx], DefaultSeed);
            glyph_state State = FindGlyphEntryByHash(Hash, Generator.GlyphTable);

            if(!State.IsRasterized)
            {
                os_glyph_info Info = Generator.Backend.FindGlyphInformation((uint32_t)Data[Idx], 16.f);

                packed_rectangle Rectangle =
                {
                    .Width  = static_cast<uint16_t>(Info.SizeX),
                    .Height = static_cast<uint16_t>(Info.SizeY),
                };

                PackRectangle(Rectangle, Generator.Packer);

                if(Rectangle.WasPacked)
                {
                    rasterized_buffer Buffer = Generator.Backend.RasterizeGlyphToAlphaTexture(Info.GlyphIndex, Info.Advance, 16.f, Generator.Arena);
                    // TODO: Figure out what to do with the buffer. (And what to store as well)

                    // TODO: Update the table.
                }
            }
        }
    }
}


static void
FillAtlas(const char *Data, uint64_t Count, glyph_generator &Generator)
{
    FillAtlas((char *)Data, Count, Generator);
}


} // namespace ntext
