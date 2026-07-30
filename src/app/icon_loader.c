#include "app/icon_loader.h"
#include "ra_api/images.h"

/* Rows fetched before the list is drawn, covering roughly one screenful. */
#define ICON_PREFETCH        6

/* Rows kept loaded around the cursor. */
#define ICON_WINDOW_BACK     2
#define ICON_WINDOW_FORWARD  8

/* Uncached rows to download per frame once the list is up. */
#define ICON_FETCH_PER_TICK  1

/* Loads one row's icon. Marked attempted first, so a failure is not retried on
   every subsequent frame. */
static void IconLoader_LoadRow(IconSlot *slot, ap_list_item *item) {
    slot->attempted = true;

    char local_path[512];
    if (RA_GetImage(slot->path, local_path, sizeof(local_path))) {
        item->image = ap_load_image(local_path);
    }
}

/* Runs every frame via ap_list_opts.footer_update, which hands us the live
   cursor and a mutable opts, so textures can be filled in as the user scrolls. */
static void IconLoader_Update(ap_list_opts *opts, int cursor, void *userdata) {
    IconLoader *loader = (IconLoader *)userdata;

    int first = cursor - ICON_WINDOW_BACK;
    int last  = cursor + ICON_WINDOW_FORWARD;
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

        IconLoader_LoadRow(slot, &opts->items[i]);
    }
}

void IconLoader_Attach(ap_list_opts *opts, IconLoader *loader) {
    opts->show_images            = true;
    opts->footer_update          = IconLoader_Update;
    opts->footer_update_userdata = loader;

    /* Fill the first screenful up front, with no per-frame budget: this runs
       before the list is drawn, so there is no frame to protect. */
    int last = loader->count < ICON_PREFETCH ? loader->count : ICON_PREFETCH;
    for (int i = 0; i < last; i++) {
        IconSlot *slot = &loader->slots[i];
        if (slot->attempted || !slot->path[0]) continue;

        IconLoader_LoadRow(slot, &opts->items[i]);
    }
}

void IconLoader_DestroyTextures(ap_list_item *items, int count) {
    for (int i = 0; i < count; i++) {
        if (items[i].image) SDL_DestroyTexture(items[i].image);
    }
}
