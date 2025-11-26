#include "src/ntext.h"

// TODO:
// Figure out how to represent utf-8. Without too much friction.

// NOTE:
// The API is still quite shaky. Unsure about this whole bitmap stuff
// (Pointer vs stack). I mean, it's mostly a persistent data makes no sense to keep
// on stack? I think it's fine. Just maybe the internals are weird?

// WARN:
// I have found a major weakness for this API:
// We persist a full bitmap in memory. A simple greyscale 1024x1024 is 1 mib of
// memory. Most of this memory is actually useless. Uhm. This is quite a big problem.

// WARN:
// So my solution was that, we just use bitmaps as the output of CloseCollection
// you basically receive an array of bitmaps to copy. Simple. But what about
// memory spikes, meaning a frame that has a big spike in memory usage, this will
// overflow the arena. But I mean, I can make that user controlled quite easily.

int main()
{
    ntext::ntext_params ContextParams =
    {
        .TextStorage       = ntext::TextStorage::LazyAtlas,
        .FrameMemoryBudget = 1024 * 10 * 10,
        .FrameMemory       = malloc(1024 * 10),
    };
    ntext::context Context = CreateContext(ContextParams);

    if(ntext::IsValidContext(Context))
    {
        // NOTE:
        // We would open some collection with a 2D allocator and a format.
        // Then we can easily rasterize a list of CPU buffers with no knowledge of anything else.
        // A collection is probably also a font, but what is TBD.

        ntext::collection Collection = ntext::OpenCollection(ntext::TextureFormat::GreyScale);

        ntext::PushStringInCollection("a", 1, Collection, Context);
        ntext::PushStringInCollection("b", 1, Collection, Context);
        ntext::PushStringInCollection("c", 1, Collection, Context);

        // NOTE:
        // Iterate Collection -> Check if we need to do anything -> Rasterize -> Allocate Into Bitmap -> Return List of copies.
        ntext::CloseCollection(Collection, Context);
    }

    return 0;
}
