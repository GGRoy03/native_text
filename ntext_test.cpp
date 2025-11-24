#include "src/ntext.h"

// TODO:
// Figure out how to represent utf-8. Without too much friction.

int main()
{
    ntext::ntext_params ContextParams =
    {
        .TextStorage       = ntext::TextStorage::LazyAtlas,
        .FrameMemoryBudget = 1024 * 10,
        .FrameMemory       = malloc(1024 * 10),
    };
    ntext::context Context = CreateContext(ContextParams);

    ntext::bitmap_params BitmapParams =
    {
        .Buffer = malloc(0),
        .Format = ntext::BitmapFormat::GreyScale,
        .Width  = 1024,
        .Height = 1024,
    };
    ntext::bitmap Bitmap = CreateBitmap(BitmapParams);

    if(ntext::IsValidContext(Context))
    {
        ntext::collection Collection = ntext::OpenCollection(Bitmap);

        ntext::PushCollection((void *)"a", Collection, Context);
        ntext::PushCollection((void *)"b", Collection, Context);
        ntext::PushCollection((void *)"c", Collection, Context);

        // NOTE:
        // Iterate Collection -> Check if we need to do anything -> Rasterize -> Allocate Into Bitmap -> Return List of copies.
        ntext::CloseCollection(Collection, Context);
    }

    return 0;
}
