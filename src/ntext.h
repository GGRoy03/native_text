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
    NTEXT_ASSERT(Arena);

    Arena->Position = sizeof(memory_arena);
}


static uint64_t
GetArenaPosition(memory_arena *Arena)
{
    NTEXT_ASSERT(Arena);

    uint64_t Result = Arena->BasePosition + Arena->Position;
    return Result;
}


static memory_region
EnterMemoryRegion(memory_arena *Arena)
{
    NTEXT_ASSERT(Arena);

    memory_region Result;
    Result.Arena    = Arena;
    Result.Position = GetArenaPosition(Arena);

    return Result;
}


static void
LeaveMemoryRegion(memory_region Region)
{
    NTEXT_ASSERT(Region.Arena);

    Region.Arena->Position = Region.Position;
}

template <typename T>
constexpr T* PushArrayNoZeroAligned(memory_arena* Arena, uint64_t Count, uint64_t Align)
{
    return static_cast<T*>(PushArena(Arena, sizeof(T) * Count, Align));
}

template <typename T>
constexpr T* PushArrayAligned(memory_arena* Arena, uint64_t Count, uint64_t Align)
{
    return PushArrayNoZeroAligned<T>(Arena, Count, Align);
}

template <typename T>
constexpr T* PushArray(memory_arena* Arena, uint64_t Count)
{
    return PushArrayAligned<T>(Arena, Count, max(8, alignof(T)));
}

template <typename T>
constexpr T* PushStruct(memory_arena* Arena)
{
    return PushArray<T>(Arena, 1);
}

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
    void    *Data;
    uint32_t Stride;
    uint32_t Width;
    uint32_t Height;
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


// TODO: Error checking

os_glyph_info
backend_context::FindGlyphInformation(uint32_t CodePoint, float EmSize)
{
    IDWriteFontFace *FontFace = this->Font;

    UINT16 GlyphIndex = 0;
    FontFace->GetGlyphIndices(&CodePoint, 1, &GlyphIndex);

    DWRITE_GLYPH_METRICS GlyphMetrics = {};
    FontFace->GetDesignGlyphMetrics(&GlyphIndex, 1, &GlyphMetrics, FALSE);

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

// TODO: Error Checking

rasterized_buffer
backend_context::RasterizeGlyphToAlphaTexture(uint16_t GlyphIndex, float Advance, float EmSize, memory_arena *Arena)
{
    rasterized_buffer Result = {};

    // Need to check for NULL here? Maybe assert since it probably needs to be done before this point.

    IDWriteFontFace *FontFace = this->Font;
    IDWriteFactory  *DWrite   = this->DirectWrite;

    UINT16              GlyphIndices[1] = {GlyphIndex};
    FLOAT               Advances[1]     = {Advance};
    DWRITE_GLYPH_OFFSET Offsets[1]      = {{0.f, 0.f}};

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

            BYTE *Buffer = PushArray<BYTE>(Arena, BufferSize);
            if(Buffer)
            {
                if(RunAnalysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &TextureBounds, Buffer, BufferSize) == S_OK)
                {
                    Result.Data          = Buffer;
                    Result.Stride        = Stride;
                    Result.Width         = TextureWidth;
                    Result.Height        = TextureHeight;
                    Result.BytesPerPixel = BytesPerPixel;
                }
            }

             static int Counter = 0;
             wchar_t OutPath[64];
             swprintf(OutPath, 64, L"glyph_alpha_%d.bmp", Counter++);
             WriteGrayscaleBMP(OutPath, TextureWidth, TextureHeight, Buffer);
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
    float Left;
    float Top;
    float Right;
    float Bottom;
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

    // This is kind of a hack. Unsure why this work since I do not fully remember how this algorithm works.
    // The caller assumes that we return the bottom left and this looks like it does the trick?
    // No it doesn't. Wait I am confused.

    Rectangle.WasPacked = 1;
    Rectangle.X         = NewTopLeft.X;
    Rectangle.Y         = NewBotRight.Y;
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

    uint16_t          GlyphIndex;
    rectangle         Source;
    glyph_layout_info Layout;
    bool              IsRasterized;
};


struct glyph_table
{
    uint8_t     *Metadata;
    glyph_entry *Buckets;

    uint64_t     GroupWidth;
    uint64_t     GroupCount;
    uint64_t     HashMask;

    uint32_t     SentinelIndex;
};


struct glyph_state
{
    uint32_t          Id;
    uint16_t          GlyphIndex;
    glyph_layout_info Layout;
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
    NTEXT_ASSERT(Index <= (Table->GroupCount * Table->GroupWidth)); // Could be sentinel.

    glyph_entry *Result = Table->Buckets + Index;
    return Result;
}

static glyph_entry *
GetGlyphTableSentinel(glyph_table *Table)
{
    NTEXT_ASSERT(Table->SentinelIndex == Table->GroupCount * Table->GroupWidth);

    glyph_entry *Result = Table->Buckets + Table->SentinelIndex;
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


// No clue how good (bad) this is bad. Trying something.

static glyph_hash
ComputeGlyphHash(size_t Count, uint32_t *Codepoints, void *Owner, char unsigned *Seedx16)
{
    NTEXT_ASSERT(Count);
    NTEXT_ASSERT(Codepoints);
    NTEXT_ASSERT(Seedx16);

    glyph_hash Result = {0};

    __m128i HashValue = _mm_set_epi64x(reinterpret_cast<uint64_t>(Owner), static_cast<uint64_t>(Count));
    HashValue = _mm_xor_si128(HashValue, _mm_loadu_si128((__m128i *)Seedx16));

    size_t ChunkCount = Count / 4;
    while(ChunkCount--)
    {
        __m128i In = _mm_loadu_si128((__m128i *)Codepoints);
        Codepoints += 4;

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
    __movsb((unsigned char *)Temp, reinterpret_cast<BYTE *>(Codepoints), Overhang);
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
    uint64_t BucketsSize  = (SlotCount + 1) * sizeof(glyph_entry); // Accounts for sentinel.
    uint64_t Result       = MetadataSize + BucketsSize + sizeof(glyph_table);

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

        Result = reinterpret_cast<glyph_table *>(Buckets + SlotCount + 1); // Skip sentinel
        Result->Metadata      = Metadata;
        Result->Buckets       = Buckets;
        Result->GroupWidth    = static_cast<uint64_t>(Params.GroupWidth);
        Result->GroupCount    = Params.GroupCount;
        Result->HashMask      = Params.GroupCount - 1; // Only used to find the group index, not the slot index
        Result->SentinelIndex = SlotCount;

        for(uint32_t Idx = 0; Idx < SlotCount; ++Idx)
        {
            glyph_entry *Entry = GetGlyphEntry(Idx, Result);
            Entry->GlyphIndex   = 0;
            Entry->Source       = {};
            Entry->Layout       = {};
            Entry->IsRasterized = false;
        }

        for(uint32_t Idx = 0; Idx < SlotCount; ++Idx)
        {
            Result->Metadata[Idx] = GlyphTableEmptyMask;
        }

        glyph_entry *Sentinel = GetGlyphTableSentinel(Result);
        NTEXT_ASSERT(Sentinel);

        Sentinel->PrevLRU = Result->SentinelIndex;
        Sentinel->NextLRU = Result->SentinelIndex;
    }

    return Result;
}


// This will never trigger the LRU. Do we do some integer counting to figure out when it is full?
// Simply recycle when we reach the maximum capacity? Sounds simple.

static glyph_state
FindGlyphEntryByHash(glyph_hash Hash, glyph_table *Table)
{
    NTEXT_ASSERT(Table);

    glyph_entry *Result     = 0;
    uint32_t     EntryIndex = GlyphTableInvalidEntry;

    uint32_t ProbeCount = 0;
    uint32_t GroupIndex = GetGlyphGroupIndexFromHash(Hash, Table);

    NTEXT_ASSERT(GroupIndex < Table->GroupCount);

    while(!Result)
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
                GroupIndex = (GroupIndex + (ProbeCount * ProbeCount)) & Table->HashMask ;
            }
            else
            {
                EntryIndex = FindFirstBit(EmptyMask) + (GroupIndex * Table->GroupWidth);
                break;
            }
        }
    }

    if(Result)
    {
        // An existing entry was found, we simply pop it from the chain.

        glyph_entry *Prev = GetGlyphEntry(Result->PrevLRU, Table);
        glyph_entry *Next = GetGlyphEntry(Result->NextLRU, Table);

        NTEXT_ASSERT(Prev);
        NTEXT_ASSERT(Next);

        Prev->NextLRU = Result->NextLRU;
        Next->PrevLRU = Result->PrevLRU;
    }
    else
    {
        // No existing entry was found, we simply allocate a new one by updating the metadata array.

        NTEXT_ASSERT(EntryIndex != GlyphTableInvalidEntry);

        // Since the tag is 00XX XXXX, we clear the state bits.
        Table->Metadata[EntryIndex] = GetGlyphTagFromHash(Hash).Value;

        Result = GetGlyphEntry(EntryIndex, Table);
        Result->Hash = Hash;
    }

    glyph_entry *Sentinel = GetGlyphTableSentinel(Table);
    NTEXT_ASSERT(Sentinel && Result != Sentinel);

    Result->NextLRU = Sentinel->NextLRU;
    Result->PrevLRU = Table->SentinelIndex;

    glyph_entry *Head = GetGlyphEntry(Sentinel->NextLRU, Table);
    Head->PrevLRU     = EntryIndex;
    Sentinel->NextLRU = EntryIndex;

    glyph_state State =
    {
        .Id           = EntryIndex,
        .GlyphIndex   = Result->GlyphIndex,
        .Layout       = Result->Layout,
        .Source       = Result->Source,
        .IsRasterized = Result->IsRasterized,
    };

    return State;
}


static glyph_state
UpdateGlyphTableEntry(uint32_t Id, bool IsRasterized, uint16_t GlyphIndex, glyph_layout_info LayoutInfo, rectangle Source, glyph_table *Table)
{
    glyph_entry *Entry = GetGlyphEntry(Id, Table);
    NTEXT_ASSERT(Entry);

    Entry->IsRasterized = IsRasterized;
    Entry->GlyphIndex   = GlyphIndex;
    Entry->Layout       = LayoutInfo;
    Entry->Source       = Source;

    glyph_state State =
    {
        .Id           = Id,
        .GlyphIndex   = GlyphIndex,
        .Layout       = LayoutInfo,
        .Source       = Source,
        .IsRasterized = IsRasterized,
    };
    
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
    uint16_t    CacheSizeX;
    uint16_t    CacheSizeY;
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

// Should we add a function to get the static footprint for the glyph generator?


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
        uint64_t Footprint = GetRectanglePackerFootprint(Params.CacheSizeX);
        void    *Memory    = PushArena(Generator.Arena, Footprint, AlignOf(void *));

        Generator.Packer = PlaceRectanglePackerInMemory(Params.CacheSizeX, Params.CacheSizeY, Memory);

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
// @Public : NText String Utilities
// ==================================================================================

struct word_slice
{
    uint64_t Start;
    uint64_t Length;
};


struct word_slice_node
{
    word_slice_node *Next;
    word_slice       Value;
};


struct word_slice_list
{
    word_slice_node *First;
    word_slice_node *Last;
    uint32_t         Count;
};


enum class TextAnalysis
{
    None               = 0,
    GenerateWordSlices = 1,
    SkipComplexCheck   = 2,
};

inline TextAnalysis operator|(TextAnalysis A, TextAnalysis B)   {return static_cast<TextAnalysis>(static_cast<int>(A) | static_cast<int>(B));}
inline TextAnalysis operator&(TextAnalysis A, TextAnalysis B)   {return static_cast<TextAnalysis>(static_cast<int>(A) & static_cast<int>(B));}


struct unicode_decode
{
    uint32_t Increment;
    uint32_t Codepoint;
};


struct analysed_text
{
    bool             IsComplex;
    word_slice_list  Words;
    uint32_t        *Codepoints;
    uint64_t         CodepointCount;
};


const static uint8_t UTF8Class[32] = 
{
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,2,2,2,2,3,3,4,5,
};


static unicode_decode 
UTF8Decode(char *String, uint64_t Maximum)
{
    unicode_decode Result = { 1, _UI32_MAX};

    uint8_t Byte      = String[0];
    uint8_t ByteClass = UTF8Class[Byte >> 3];

    switch (ByteClass)
    {

    case 1:
    {
        Result.Codepoint = Byte;
    } break;

    case 2:
    {
        if (1 < Maximum)
        {
            char ContByte = String[1];
            if (UTF8Class[ContByte >> 3] == 0)
            {
                Result.Codepoint  = (Byte     & 0b00011111) << 6;
                Result.Codepoint |= (ContByte & 0b00111111) << 0;
                Result.Increment  = 2;
            }
        }
    } break;

    case 3:
    {
        if (2 < Maximum)
        {
            char ContByte[2] = { String[1], String[2] };
            if (UTF8Class[ContByte[0] >> 3] == 0 && UTF8Class[ContByte[1] >> 3] == 0)
            {
                Result.Codepoint  = ((Byte        & 0b00001111) << 12);
                Result.Codepoint |= ((ContByte[0] & 0b00111111) << 6 );
                Result.Codepoint |= ((ContByte[1] & 0b00111111) << 0 );
                Result.Increment  = 3;
            }
        }
    } break;

    case 4:
    {
        if (3 < Maximum)
        {
            char ContByte[3] = { String[1], String[2], String[3] };
            if (UTF8Class[ContByte[0] >> 3] == 0 && UTF8Class[ContByte[1] >> 3] == 0 && UTF8Class[ContByte[2] >> 3] == 0)
            {
                Result.Codepoint  = (Byte        & 0b00000111) << 18;
                Result.Codepoint |= (ContByte[0] & 0b00111111) << 12;
                Result.Codepoint |= (ContByte[1] & 0b00111111) << 6;
                Result.Codepoint |= (ContByte[2] & 0b00111111) << 0;
                Result.Increment = 4;
            }
        }
    } break;

    }

    return Result;
}


static analysed_text
AnalyzeText(char *Data, uint64_t Size, TextAnalysis Flags, glyph_generator &Generator)
{
    NTEXT_ASSERT(Data);
    NTEXT_ASSERT(Size);

    analysed_text Result =
    {
        .Codepoints = PushArray<uint32_t>(Generator.Arena, Size),
    };

    if(Result.Codepoints)
    {
        uint64_t DecodedBytes = 0;

        while(DecodedBytes < Size)
        {
            unicode_decode Decoded = UTF8Decode(Data + DecodedBytes, Size - DecodedBytes);

            Result.Codepoints[Result.CodepointCount++] = Decoded.Codepoint;
            DecodedBytes                              += Decoded.Increment;
        }

        if(!((Flags & TextAnalysis::SkipComplexCheck) != TextAnalysis::None))
        {
            char    *At                = Data;
            uint64_t Remaining         = Size;
            uint64_t Advance           = 16;
            __m128i  ComplexByteVector = _mm_set1_epi8(0x80);

            __m128i HasComplex = _mm_setzero_si128();
            while(Remaining >= Advance)
            {
                __m128i Batch = _mm_loadu_si128((__m128i *)At);
                __m128i TextX = _mm_and_si128(Batch, ComplexByteVector);

                HasComplex = _mm_or_si128(HasComplex, TextX);

                if(_mm_movemask_epi8(HasComplex))
                {
                    Result.IsComplex = true;
                    break;
                }

                Remaining -= Advance;
                At        += Advance;
            }

            if(!Result.IsComplex)
            {
                while(Remaining--)
                {
                    if(*At == (char)0x80)
                    {
                        Result.IsComplex = true;
                        break;
                    }
                }
            }
        }

        if((Flags & TextAnalysis::GenerateWordSlices) != TextAnalysis::None)
        {
            // We only handle simple ASCII path for now. Which is fine.

            uint64_t CodepointIdx = 0;

            while(CodepointIdx < Result.CodepointCount)
            {
                uint32_t Codepoint = Result.Codepoints[CodepointIdx];
        
                if(Codepoint != ' ' && Codepoint != '\t')
                {
                    word_slice_node *Node = PushStruct<word_slice_node>(Generator.Arena);
                    if(Node)
                    {
                        word_slice_list &List = Result.Words;

                        Node->Value.Start  = CodepointIdx;
                        Node->Value.Length = 0;
        
                        if(!List.First)
                        {
                             List.First = Node;
                        }
            
                        if(List.Last)
                        {
                            List.Last->Next = Node;
                        }
            
                        List.Last   = Node;
                        List.Count += 1;
        
                        Codepoint = Data[++CodepointIdx];
                        while(CodepointIdx < Result.CodepointCount && Codepoint != ' ' && Codepoint != '\t')
                        {
                            CodepointIdx += 1;
                            Codepoint     = Result.Codepoints[CodepointIdx];
                        }
        
                        Node->Value.Length = CodepointIdx - Node->Value.Start;
                    }
                    else
                    {
                        break;
                    }
                }
                else
                {
                    ++CodepointIdx;
                }
            }

        }
    }


    return Result;
}

// ==================================================================================
// @Public : NText Collection
// ==================================================================================

enum class TextureFormat
{
    None      = 0,
    RGBA      = 1,
    GreyScale = 2,
};


struct rasterized_glyph
{
    rectangle         Source;
    rasterized_buffer Buffer;
};


struct rasterized_glyph_node
{
    rasterized_glyph_node *Next;
    rasterized_glyph       Value;
};


struct rasterized_glyph_list
{
    rasterized_glyph_node *First;
    rasterized_glyph_node *Last;
    uint32_t               Count;
};


struct shaped_glyph
{
    uint16_t          GlyphIndex;
    rectangle         Source;
    glyph_layout_info Layout;
    uint32_t          ClusterStart;
    uint32_t          ClusterCount;
};


struct shaped_glyph_run
{
    rasterized_glyph_list UpdateList;
    shaped_glyph         *Shaped;
    uint32_t              ShapedCount;
};


struct word_glyph_cursor
{
    shaped_glyph *Glyphs;
    uint32_t      GlyphCount;
    uint32_t      GlyphAt;
};


struct word_advance
{
    float    Value;
    uint64_t End;
};


// TODO: Error checks.

static shaped_glyph_run
FillAtlas(analysed_text Analysed, glyph_generator &Generator)
{
    shaped_glyph_run Run = {};
    Run.Shaped = PushArray<shaped_glyph>(Generator.Arena, Analysed.CodepointCount);

    // Wait this is the fast path.

    if(!Analysed.IsComplex)
    {
        for(uint32_t Idx = 0; Idx < Analysed.CodepointCount; ++Idx)
        {
            uint32_t Codepoint = Analysed.Codepoints[Idx];
        
            glyph_hash  Hash  = ComputeGlyphHash(1, &Codepoint, 0, DefaultSeed);
            glyph_state State = FindGlyphEntryByHash(Hash, Generator.GlyphTable);
        
            if(!State.IsRasterized)
            {
                // We need to figure out fonts, instead of harcoding values.
        
                os_glyph_info Info = Generator.Backend.FindGlyphInformation(Codepoint, 16.f);
        
                // This cast is wrong/dangerous. Should probably round up or allow floating points in the packer?
        
                packed_rectangle Rectangle =
                {
                    .Width  = static_cast<uint16_t>(Info.SizeX),
                    .Height = static_cast<uint16_t>(Info.SizeY),
                };
        
                PackRectangle(Rectangle, Generator.Packer);
        
                if(Rectangle.WasPacked)
                {
                    // This is just wrong. At least, what we return from the packer is confusing.
        
                    rectangle Source =
                    {
                        .Left   = static_cast<float>(Rectangle.X),
                        .Top    = static_cast<float>(Rectangle.Y),
                        .Right  = static_cast<float>(Rectangle.X + Rectangle.Width ),
                        .Bottom = static_cast<float>(Rectangle.Y + Rectangle.Height),
                    };
        
                    rasterized_buffer Buffer = Generator.Backend.RasterizeGlyphToAlphaTexture(Info.GlyphIndex, Info.Advance, 16.f, Generator.Arena);
        
                    if(Buffer.BytesPerPixel == 1 && Buffer.Data)
                    {
                        auto *Node = PushStruct<rasterized_glyph_node>(Generator.Arena);
                        if(Node)
                        {
                            rasterized_glyph_list &List = Run.UpdateList;
        
                            Node->Value.Buffer = Buffer;
                            Node->Value.Source = Source;
        
                            if(!List.First)
                            {
                                List.First = Node;
                            }
        
                            if(List.Last)
                            {
                                List.Last->Next = Node;
                            }
        
                            List.Last   = Node;
                            List.Count += 1;
                        }
                    }
        
                    glyph_layout_info LayoutInfo =
                    {
                        .Advance = Info.Advance,
                        .OffsetX = Info.OffsetX,
                        .OffsetY = Info.OffsetY,
                    };
        
                    State = UpdateGlyphTableEntry(State.Id, 1, Info.GlyphIndex, LayoutInfo, Source, Generator.GlyphTable);
                }
            }
        
            Run.Shaped[Run.ShapedCount++] = 
            {
                .GlyphIndex   = State.GlyphIndex,
                .Source       = State.Source,
                .Layout       = State.Layout,
                .ClusterStart = Idx,
                .ClusterCount = 1,
            };
        }
    }
    else
    {
        NTEXT_ASSERT(!"TODO: Implement Complex String Parsing");
    }


    return Run;
}

static float
AdvanceWord(word_glyph_cursor &Cursor, const word_slice &Slice)
{
    float Advance = {};

    uint64_t WordStart = Slice.Start;
    uint64_t WordEnd   = Slice.Start + Slice.Length;

    for(uint32_t Idx = Cursor.GlyphAt; Idx < Cursor.GlyphCount; ++Idx)
    {
        uint64_t GlyphStart = Cursor.Glyphs[Idx].ClusterStart;
        uint64_t GlyphEnd   = GlyphStart + Cursor.Glyphs[Idx].ClusterCount;

        if(GlyphEnd <= WordStart)
        {
            continue;
        }

        if(GlyphStart >= WordEnd)
        {
            Cursor.GlyphAt = Idx;
            break;
        }

        Advance       += Cursor.Glyphs[Idx].Layout.Advance;
        Cursor.GlyphAt = Idx + 1;
    }

    return Advance;
}

} // namespace ntext
