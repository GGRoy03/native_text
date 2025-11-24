#include <immintrin.h>
#include <stdint.h>

namespace ntext
{

// ====================================================================================
// @Internal : Context Cracking

#ifdef _WIN32
#define NTEXT_WIN32 1
#endif

// ====================================================================================
// @Internal : Various Utilities

#define NTEXT_ASSERT(Cond) do {if (!(Cond)) __assume(0);} while (0)
#define NTEXT_ALIGNPOW2(x,b) (((x) + (b) - 1)&(~((b) - 1)))

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

// ====================================================================================
// @Internal : Win32 Implementation

#ifdef NTEXT_WIN32

#include <dwrite.h>
#include <windows.h>


#endif // NTEXT_WIN32

// ====================================================================================
// @Internal : Rectangle Packing

// NOTE:
// Perhaps use some third-party one first? Just to focus on the text rasterizing?
// In any case, it seems like Skyline is the best method? Seems like Shelf/Rows might be usable as well.
// Need to think about offline packing as well.

// ====================================================================================
// @Public : NText Bitmaps

enum class BitmapFormat
{
    None      = 0,
    RGBA      = 1,
    GreyScale = 2,
};

struct bitmap_params
{
    void        *Buffer;
    BitmapFormat Format;
    uint32_t     Width;
    uint32_t     Height;
};

struct bitmap
{
    void        *Buffer;
    uint32_t     Width;
    uint32_t     Height;
    BitmapFormat Format;
};

static bitmap CreateBitmap(bitmap_params Params)
{
    bitmap Result =
    {
        .Buffer = Params.Buffer,
        .Width  = Params.Width,
        .Height = Params.Height,
        .Format = Params.Format,
    };

    return Result;
}

static bool IsValidBitmap(bitmap Bitmap)
{
    bool Result = (Bitmap.Buffer) && (Bitmap.Width > 0 && Bitmap.Height > 0) && (Bitmap.Format != BitmapFormat::None);
    return Result;
}

// ====================================================================================
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

struct collection_item
{
    void *UTF8;
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

    bitmap Bitmap;
};

struct copy_information
{
    void *Something;
};

struct context
{
    TextStorage   TextStorage;
    memory_arena *Arena;
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

    return Context;
}

static bool IsValidContext(context &Context)
{
    bool Result = (Context.Arena != 0);
    return Result;
}

static collection OpenCollection(bitmap Bitmap)
{
    collection Collection =
    {
        .First  = 0,
        .Last   = 0,
        .Count  = 0,
        .Bitmap = Bitmap,
    };

    return Collection;
}

static void PushCollection(void *Something, collection &Collection, context &Context)
{
}

static copy_information CloseCollection(collection &Collection, context &Context)
{
    copy_information Result = {};

    switch(Context.TextStorage)
    {

    case TextStorage::LazyAtlas:
    {
    } break;

    case TextStorage::None:
    {
        NTEXT_ASSERT(!"Invalid State");
    } break;

    }

    return Result;
}

}
