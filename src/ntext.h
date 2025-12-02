// ==================================================================================
// @Internal : Includes & Context Cracking

#include <immintrin.h>
#include <stdint.h>

#define NTEXT_ASSERT(Cond) do {if (!(Cond)) __assume(0);} while (0)
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

// ====================================================================================
// @Internal : Various Utilities

struct memory_arena
{
    uint64_t Reserved;
    uint64_t BasePosition;
    uint64_t Position;
};

typedef struct memory_region
{
    memory_arena *Arena;
    uint64_t      Position;
} memory_region;

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

// ====================================================================================
// @Internal : Win32 Implementation

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

// NOTE:
// This is not quite correct, because we do not want to force certain fonts. But I need to test a bunch of things.

struct os_context
{
    IDWriteFactory  *DirectWrite;
    IDWriteFontFace *Font;
};

static os_context CreateOSContext(void)
{
    os_context Context = {};

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

static bool IsValidOSContext(os_context Context)
{
    bool Result = (Context.DirectWrite) && (Context.Font);
    return Result;
}

static void
TryRasterizeAndDump(os_context Context, memory_arena *Arena)
{
    IDWriteFontFace *FontFace = Context.Font;
    IDWriteFactory  *DWrite   = Context.DirectWrite;

    const wchar_t *Text       = L"Hello, DWrite";
    const UINT32   TextLength = (UINT32)wcslen(Text);

    UINT32 *Codepoints = (UINT32 *)PushArray(Arena, UINT32, TextLength);
    for(UINT32 Idx = 0; Idx < TextLength; ++Idx)
    {
        Codepoints[Idx] = (UINT32)Text[Idx];
    }

    UINT16 *GlyphIndices = (UINT16 *)PushArray(Arena, UINT16, TextLength);
    FontFace->GetGlyphIndices(Codepoints, TextLength, GlyphIndices);

    UINT32 GlyphCount = TextLength;

    DWRITE_FONT_METRICS FontMetrics = {};
    FontFace->GetMetrics(&FontMetrics);
    UINT32 DesignUnitsPerEm = FontMetrics.designUnitsPerEm;

    DWRITE_GLYPH_METRICS *GlyphMetrics = (DWRITE_GLYPH_METRICS *)PushArray(Arena, DWRITE_GLYPH_METRICS, GlyphCount);

    FontFace->GetDesignGlyphMetrics(GlyphIndices, GlyphCount, GlyphMetrics, FALSE);

    FLOAT  FontEmSize = 64.0f;
    FLOAT *Advances   = (FLOAT *)PushArray(Arena, FLOAT, GlyphCount);
    FLOAT  Scale      = FontEmSize / (FLOAT)DesignUnitsPerEm;

    for(UINT32 Idx = 0; Idx < GlyphCount; ++Idx)
    {
        Advances[Idx] = (FLOAT)GlyphMetrics[Idx].advanceWidth * Scale;
    }

    DWRITE_GLYPH_OFFSET *Offsets = (DWRITE_GLYPH_OFFSET *)PushArray(Arena, DWRITE_GLYPH_OFFSET, GlyphCount);
    for(UINT32 Idx = 0; Idx < GlyphCount; ++Idx)
    {
        Offsets[Idx].advanceOffset  = 0.0f;
        Offsets[Idx].ascenderOffset = 0.0f;
    }

    // NOTE:
    // This is specified as containing all of the information needed to draw.
    // Hence, if I just store this information, then the user should be able
    // to do anything? Now the problem is: Do we just rasterize glyph by glyph?
    // Do we allow more? For simplicity we could force glyph by glyph for now.

    DWRITE_GLYPH_RUN GlyphRun = {};
    GlyphRun.fontFace      = FontFace;
    GlyphRun.fontEmSize    = FontEmSize;
    GlyphRun.glyphCount    = GlyphCount;
    GlyphRun.glyphIndices  = GlyphIndices;
    GlyphRun.glyphAdvances = Advances;
    GlyphRun.glyphOffsets  = Offsets;
    GlyphRun.bidiLevel     = 0;
    GlyphRun.isSideways    = FALSE;

    IDWriteGlyphRunAnalysis *RunAnalysis;
    DWrite->CreateGlyphRunAnalysis(
        &GlyphRun,
        1.0f,
        0,
        DWRITE_RENDERING_MODE_ALIASED,
        DWRITE_MEASURING_MODE_NATURAL,
        0.0f,
        0.0f,
        &RunAnalysis);

    RECT TexBounds = {};
    RunAnalysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &TexBounds);

    INT TexW = TexBounds.right - TexBounds.left;
    INT TexH = TexBounds.bottom - TexBounds.top;

    UINT  BytesPerPixel   = 1;
    UINT  BufferSize      = TexW * TexH * BytesPerPixel;
    BYTE *AlphaBuffer     = (BYTE *)PushArray(Arena, BYTE, BufferSize);
    RECT  RequestedBounds = TexBounds;

    RunAnalysis->CreateAlphaTexture(
        DWRITE_TEXTURE_ALIASED_1x1,
        &RequestedBounds,
        AlphaBuffer,
        BufferSize);

    const wchar_t *OutPath = L"glyph_alpha.bmp";
    WriteGrayscaleBMP(OutPath, TexW, TexH, AlphaBuffer);
}

#endif // NTEXT_WIN32

// ====================================================================================
// @Internal : Rectangle Packing

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
CreateRectanglePacker(uint16_t Width, uint16_t Height, void *Memory)
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
PackRectangle(packed_rectangle &Rectangle, rectangle_packer &Packer)
{
    uint16_t Width  = Rectangle.Width;
    uint16_t Height = Rectangle.Height;

    if(Width == 0 || Height == 0)
    {
        return;
    }

    uint16_t BestIndexInclusive = UINT16_MAX;
    uint16_t BestIndexExclusive = UINT16_MAX;
    point    BestPoint          = {UINT16_MAX, UINT16_MAX};

    NTEXT_ASSERT(Packer.SkylineCount);

    for(uint16_t Idx = 0; Idx < Packer.SkylineCount; ++Idx)
    {
        point Point = Packer.Skyline[Idx];

        // If on the current skyline we do not have horizontal space, then we can break since the array is sorted left to right.
        if(Width > Packer.Width - Point.X)
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
        for(IndexExclusive = Idx + 1; IndexExclusive < Packer.SkylineCount; ++IndexExclusive)
        {
            // XMax is the right edge of the current rectangle. If it is smaller or equal then we know that
            // there is no way to intersect on the X axis anymore, because the skylines are sorted from left to right.
            if(XMax <= Packer.Skyline[IndexExclusive].X)
            {
                break;
            }

            // At this point, we know we are intersecting on the X axis, thus we need to raise the skyline,
            // which is what creates dead space. As long as we intersect on the X axis, we have to continue raising the
            // point.
            if(Point.Y < Packer.Skyline[IndexExclusive].Y)
            {
                Point.Y = Packer.Skyline[IndexExclusive].Y;
            }
        }

        // Then it's a worse point than our current best.
        if(Point.Y >= BestPoint.Y)
        {
            continue;
        }

        // Not enough vertical space to store this rectangle.
        if(Height > Packer.Height - Point.Y)
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
    point NewBotRight = {static_cast<uint16_t>(BestPoint.X + Width), Packer.Skyline[BestIndexExclusive - 1].Y};

    // To know if we are creating a new skyline from the bottom right point, we need to check multiple cases.
    // If the first exclusive point is a valid index, then we simply check: if (smaller) new point else no.
    // If the first exclusive point is not a valid index, then we simply check: if(not_on_boundary) new point

    bool HasBottomRightPoint = BestIndexExclusive < Packer.SkylineCount ?
        NewBotRight.X < Packer.Skyline[BestIndexExclusive].X :
        NewBotRight.X < Packer.Width;

    uint16_t InsertedCount = 1 + HasBottomRightPoint;
    NTEXT_ASSERT(Packer.SkylineCount + InsertedCount - RemovedCount <= Packer.Width);

    if(InsertedCount > RemovedCount)
    {
        // Copy from the last element to the exclusive index.
        // Make space for the new elements by shifting end by (InsertedCount - RemovedCount)
        // This grows the array.

        uint16_t Start = Packer.SkylineCount - 1;
        uint16_t End   = Start + (InsertedCount - RemovedCount);

        for(; Start >= BestIndexExclusive; --Start, --End)
        {
            Packer.Skyline[End] = Packer.Skyline[Start];
        }

        Packer.SkylineCount += (InsertedCount - RemovedCount);
    } else
    if(InsertedCount < RemovedCount)
    {
        // Copy from the first exclusive element to (Start - ElementsToRemoveCount)
        // We basically start copying behind us and iterate until the end of the array.

        uint16_t Start = BestIndexExclusive;
        uint16_t End   = Start - (RemovedCount - InsertedCount);

        for(; Start < Packer.SkylineCount; ++Start, ++End)
        {
            Packer.Skyline[End] = Packer.Skyline[Start];
        }

        Packer.SkylineCount += (RemovedCount - InsertedCount);
    }

    Packer.Skyline[BestIndexInclusive] = NewTopLeft;
    if(HasBottomRightPoint)
    {
        Packer.Skyline[BestIndexInclusive + 1] = NewBotRight;
    }

    Rectangle.WasPacked = 1;
    Rectangle.X         = NewTopLeft.X;
    Rectangle.Y         = NewTopLeft.Y;
}

// ====================================================================================
// @Public : NText Glyph Table Implementation

enum class GlyphTableWidth
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

struct glyph_entry
{
    glyph_hash Hash;

    uint64_t PrevLRU;
    uint64_t NextLRU;

    uint16_t GlyphIndex;
    float    SizeX;
    float    SizeY;
    float    Advance;
    float    OffsetX;
    float    OffsetY;
    bool     IsRasterized; // NOTE: Prefer state?
};

struct glyph_table
{
    uint8_t     *Metadata;
    glyph_entry *Buckets;
    glyph_entry  Sentinel;

    uint64_t GroupWidth;
    uint64_t GroupCount;
    uint64_t HashMask;
};

struct glyph_state
{
    uint64_t Id;
    bool     IsRasterized;
};

struct shaped_glyph
{
    uint16_t GlyphIndex;
    float    Advance;
    float    OffsetX;
    float    OffsetY;
};

constexpr uint8_t GlyphTableEmptyMask = 1 << 0;
constexpr uint8_t GlyphTableDeadMask  = 1 << 1;
constexpr uint8_t GlyphTableTagMask   = 0xFF & ~0x03;

static bool
IsValidGlyphTable(glyph_table *Table)
{
    bool Result = (Table) && (Table->Metadata) && (Table->Buckets);
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

static uint64_t
GetFreeGlyphEntry(glyph_table *Table)
{
    uint64_t Result = 0;
    return Result;
}

static uint64_t
GetGlyphGroupIndexFromHash(glyph_hash Hash, glyph_table *Table)
{
    uint64_t Result = 0;

    return Result;
}

// NOTE:
// I have no idea what this is. It is taken directly from refterm. Casey mentions tha
// the hash might not be the best, but I don't know any better. Let's stick with
// this for now.

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

    // NOTE:
    // I feel like I remember seeing a fix for this? Need to check.

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
    glyph_tag Result = {};
    return Result;
}

static bool
GlyphHashesAreEqual(glyph_hash A, glyph_hash B)
{
    bool Result = 1;
    return Result;
}

static glyph_state
FindGlyphEntryByHash(glyph_hash Hash, glyph_table *Table)
{
    NTEXT_ASSERT(Table);

    glyph_entry *Result     = 0;
    uint64_t     EntryIndex = 0;

    uint64_t ProbeCount = 0;
    uint64_t GroupIndex = GetGlyphGroupIndexFromHash(Hash, Table);

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

        // WARN:
        // We are missing the allocation part. We have to mark it.
        // We have the entry index so.. We just have to compute the tag and clear
        // relevant bits inside the metadata?

        uint64_t EntryIndex = GetFreeGlyphEntry(Table);
        NTEXT_ASSERT(EntryIndex);

        Result = GetGlyphEntry(EntryIndex, Table);
        Result->Hash = Hash;
    }

    glyph_entry *Sentinel = &Table->Sentinel;
    Result->NextLRU = Sentinel->NextLRU;
    Result->PrevLRU = 0;

    glyph_entry *LastNext = GetGlyphEntry(Sentinel->NextLRU, Table);
    LastNext->PrevLRU = EntryIndex;
    Sentinel->NextLRU = EntryIndex;

    glyph_state State = {};
    State.IsRasterized = 0;
    State.Id           = EntryIndex;

    return State;
}

// ==================================================================================
// @Public : NText Context

enum class TextStorage
{
    None      = 0,
    LazyAtlas = 1,
};

struct ntext_params
{
    TextStorage TextStorage;
    uint64_t    FrameMemoryBudget;
    void       *FrameMemory;
};

struct context
{
    TextStorage   TextStorage;
    memory_arena *Arena;
    os_context    OS;
};

static context CreateContext(ntext_params Params)
{
    context Context = {};

    if(Params.FrameMemoryBudget == 0 || Params.FrameMemory == 0 || Params.TextStorage == TextStorage::None)
    {
        return Context;
    }

    // Arena Initialization
    {
        NTEXT_ASSERT(Params.FrameMemory);
        NTEXT_ASSERT(Params.FrameMemoryBudget);

        Context.Arena = static_cast<memory_arena *>(Params.FrameMemory);
        Context.Arena->Reserved     = Params.FrameMemoryBudget;
        Context.Arena->BasePosition = 0;
        Context.Arena->Position     = sizeof(memory_arena);
    }

    // Constant Forwarding
    {
        NTEXT_ASSERT(Params.TextStorage != TextStorage::None);

        Context.TextStorage = Params.TextStorage;
    }

    // OS Initialization
    {
        Context.OS = CreateOSContext();
    }

    return Context;
}

static bool IsValidContext(context &Context)
{
    bool Result = (Context.Arena != 0);
    return Result;
}

// ===================================================================================
// @Public : NText Collection

enum class TextureFormat
{
    None      = 0,
    RGBA      = 1,
    GreyScale = 2,
};

struct collection_item
{
    char    *Data;
    uint64_t Count;
    bool     ContainsComplex;
};

struct collection_node
{
    collection_node *Next;
    collection_item  Value;
};

struct collection
{
    collection_node *First;
    collection_node *Last;
    uint32_t         Count;

    TextureFormat    Format;
    rectangle_packer Packer; // NOTE: Pointer?

    void AddItem(char *String      , size_t Count, context &Context);
    void AddItem(const char *String, size_t Count, context &Context);
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

static collection OpenCollection(TextureFormat Format)
{
    collection Collection =
    {
        .First  = 0,
        .Last   = 0,
        .Count  = 0,
        .Format = Format,
    };

    return Collection;
}

void collection::AddItem(const char *Data, size_t Count, context &Context)
{
    collection::AddItem((char *)Data, Count, Context);
}

void collection::AddItem(char *Data, size_t Count, context &Context)
{
    collection_item Item = {.Data = Data, .Count = Count};

    uint32_t Advance     = 16;
    __m128i  ComplexByte = _mm_set1_epi8(0x80);

    while(Count)
    {
        __m128i ContainsComplex = _mm_setzero_si128();

        while(Count >= Advance)
        {
            __m128i Batch = _mm_loadu_si128((__m128i *) Data);
            __m128i TestX = _mm_and_si128(Batch, ComplexByte);

            // NOTE: If we break early, is there a better instruction than a OR?
            ContainsComplex = _mm_or_si128(ContainsComplex, TestX);
            if(_mm_movemask_epi8(ContainsComplex))
            {
                break;
            }

            Count -= Advance;
            Data  += Advance;
        }

        Item.ContainsComplex = _mm_movemask_epi8(ContainsComplex);
        if(Item.ContainsComplex)
        {
            break;
        }

        char Token = Data[0];
        if(Token < 0)
        {
            Item.ContainsComplex = 1;
            break;
        }

        Data++;
        Count--;
    }

    collection_node *Node = PushStruct(Context.Arena, collection_node);
    if (Node)
    {
        Node->Value = Item;

        if (!this->First)
        {
            this->First = Node;
        }

        if (this->Last)
        {
            this->Last->Next = Node;
        }

        this->Last = Node;
    }
}

static void
CloseCollection(collection &Collection, context &Context)
{
    switch(Context.TextStorage)
    {

    case TextStorage::LazyAtlas:
    {
        for(collection_node *Node = Collection.First; Node != 0; Node = Node->Next)
        {
            collection_item Item = Node->Value;

            if(!Item.ContainsComplex)
            {
                for(uint64_t Idx = 0; Idx < Item.Count; ++Idx)
                {
                    glyph_hash  Hash  = ComputeGlyphHash(1, (char unsigned *) &Item.Data[Idx], DefaultSeed);
                    glyph_state State = FindGlyphEntryByHash(Hash, 0);
                    if(!State.IsRasterized)
                    {
                        // TODO: Rasterize and whatnot. Need to shape first.
                    }
                }
            }
            else
            {
                NTEXT_ASSERT(!"Not Implemented");
            }
        }
    } break;

    case TextStorage::None:
    {
        NTEXT_ASSERT(!"Invalid State");
    } break;

    }
}

}
