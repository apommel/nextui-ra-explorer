#include "app/icon_loader.h"
#include "ra_api/images.h"

/* Rows fetched before the list is drawn, covering roughly one screenful. */
#define ICON_PREFETCH        6

/* Rows kept loaded around the cursor. */
#define ICON_WINDOW_BACK     2
#define ICON_WINDOW_FORWARD  8

/* Turns a row's cached file into a texture. Downloads never happen here: the
   file is either already cached or the row stays iconless. */
static void IconLoader_AttachTexture(IconLoader *loader, ap_list_item *items, int row) {
    if (row < 0 || row >= loader->count) return;

    char local_path[512];
    if (RA_GetCachedImage(loader->slots[row].path, local_path, sizeof(local_path))) {
        items[row].image = ap_load_image(local_path);
    }
}

/* Queues every row in [first, last] that still needs downloading, until the
   fetcher runs out of slots. Rows already cached are turned into textures on
   the spot, since that costs no network. */
static void IconLoader_FillWindow(IconLoader *loader, ap_list_item *items,
                                  int first, int last) {
    for (int row = first; row <= last; row++) {
        IconSlot *slot = &loader->slots[row];
        if (slot->attempted || !slot->path[0]) continue;

        char local_path[512];
        if (RA_GetCachedImage(slot->path, local_path, sizeof(local_path))) {
            slot->attempted = true;
            items[row].image = ap_load_image(local_path);
            continue;
        }

        /* Marked attempted only once queued, so a full fetcher simply means
           this row is retried on a later frame. */
        if (!RA_ImageFetchStart(loader->fetcher, slot->path, row)) break;
        slot->attempted = true;
    }
}

/* Runs every frame via ap_list_opts.footer_update, which hands us the live
   cursor and a mutable opts, so textures can be filled in as the user scrolls. */
static void IconLoader_Update(ap_list_opts *opts, int cursor, void *userdata) {
    IconLoader *loader = (IconLoader *)userdata;

    /* Advance downloads and take delivery of anything that finished. Neither
       step blocks, so the frame is never held up by the network. */
    int done[RA_IMAGE_FETCH_MAX];
    int count = RA_ImageFetchPump(loader->fetcher, done, RA_IMAGE_FETCH_MAX);
    for (int i = 0; i < count; i++) {
        IconLoader_AttachTexture(loader, opts->items, done[i]);
    }

    int first = cursor - ICON_WINDOW_BACK;
    int last  = cursor + ICON_WINDOW_FORWARD;
    if (first < 0) first = 0;
    if (last >= loader->count) last = loader->count - 1;

    IconLoader_FillWindow(loader, opts->items, first, last);
}

void IconLoader_Attach(ap_list_opts *opts, IconLoader *loader) {
    opts->show_images            = true;
    opts->footer_update          = IconLoader_Update;
    opts->footer_update_userdata = loader;

    loader->fetcher = RA_ImageFetchCreate();

    /* Fill the first screenful before the list is drawn. This is the one place
       that waits, deliberately: there is no frame to protect yet, and the
       transfers still run concurrently. */
    int last = loader->count < ICON_PREFETCH ? loader->count : ICON_PREFETCH;
    IconLoader_FillWindow(loader, opts->items, 0, last - 1);

    int done[RA_IMAGE_FETCH_MAX];
    int count = RA_ImageFetchWait(loader->fetcher, done, RA_IMAGE_FETCH_MAX);
    for (int i = 0; i < count; i++) {
        IconLoader_AttachTexture(loader, opts->items, done[i]);
    }
}

void IconLoader_Release(IconLoader *loader, ap_list_item *items, int count) {
    /* Cancels any transfer still in flight, discarding its partial file. */
    RA_ImageFetchDestroy(loader->fetcher);
    loader->fetcher = NULL;

    for (int i = 0; i < count; i++) {
        if (items[i].image) SDL_DestroyTexture(items[i].image);
    }
}
