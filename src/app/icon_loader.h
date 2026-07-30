#ifndef ICON_LOADER_H
#define ICON_LOADER_H

#include <stdbool.h>

#include "apostrophe.h"
#include "apostrophe_widgets.h"

/* Lazy row icons for ap_list.

   Downloading every row's icon up front makes a long list take as long to open
   as it has rows. Instead each row carries its remote path, and icons are
   fetched only for rows near the cursor, driven by ap_list's per-frame
   callback, so the list opens immediately and fills in as the user scrolls.

   Usage: fill one IconSlot per row with its remote path, point an IconLoader at
   them, call IconLoader_Attach before ap_list, then IconLoader_Release after it
   returns. The slots must outlive the ap_list call. */

#define ICON_PATH_MAX 80

typedef struct {
    char path[ICON_PATH_MAX]; /* remote RA path, empty when the row has none */
    bool attempted;           /* set once, so failures are not retried forever */
} IconSlot;

typedef struct RA_ImageFetcher RA_ImageFetcher;

typedef struct {
    IconSlot        *slots;
    int              count;
    RA_ImageFetcher *fetcher; /* owned between Attach and Release */
} IconLoader;

/* Installs the loader on opts and enables the list's image column.

   Also loads the first screenful before returning, so the list is not drawn
   half-empty — on a cold cache this waits for those few downloads, which run
   concurrently. Call it once opts->items is populated and just before ap_list. */
void IconLoader_Attach(ap_list_opts *opts, IconLoader *loader);

/* Cancels any download still running and frees every texture the loader made.
   Must be called once for each IconLoader_Attach. */
void IconLoader_Release(IconLoader *loader, ap_list_item *items, int count);

#endif /* ICON_LOADER_H */
