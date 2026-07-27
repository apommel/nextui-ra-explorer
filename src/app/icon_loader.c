#include "app/icon_loader.h"
#include "ra_api/images.h"

#define ICON_WINDOW         12 /* rows around the cursor to keep loaded */
#define ICON_FETCH_PER_TICK  1 /* uncached rows to download per frame */

/* Runs every frame via ap_list_opts.footer_update, which hands us the live
   cursor and a mutable opts, so textures can be filled in as the user scrolls. */
static void IconLoader_Update(ap_list_opts *opts, int cursor, void *userdata) {
    IconLoader *loader = (IconLoader *)userdata;

    int first = cursor - ICON_WINDOW;
    int last  = cursor + ICON_WINDOW;
    if (first < 0) first = 0;
    if (last >= loader->count) last = loader->count - 1;

    int fetched = 0;
    for (int i = first; i <= last; i++) {
        IconSlot *slot = &loader->slots[i];
        if (slot->attempted || !slot->path[0]) continue;

        /* Cache hits are just a texture upload, so take as many as we like.
           Downloads block the frame, so spend a small budget and resume next
           frame — the list stays responsive and icons stream in. */
        bool cached = RA_IsImageCached(slot->path);
        if (!cached && fetched >= ICON_FETCH_PER_TICK) break;
        if (!cached) fetched++;

        slot->attempted = true;

        char local_path[512];
        if (RA_GetImage(slot->path, local_path, sizeof(local_path))) {
            opts->items[i].image = ap_load_image(local_path);
        }
    }
}

void IconLoader_Attach(ap_list_opts *opts, IconLoader *loader) {
    opts->show_images            = true;
    opts->footer_update          = IconLoader_Update;
    opts->footer_update_userdata = loader;
}

void IconLoader_DestroyTextures(ap_list_item *items, int count) {
    for (int i = 0; i < count; i++) {
        if (items[i].image) SDL_DestroyTexture(items[i].image);
    }
}
