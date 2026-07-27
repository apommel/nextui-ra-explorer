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
   them, call IconLoader_Attach before ap_list, then IconLoader_DestroyTextures
   after it returns. The slots must outlive the ap_list call. */

#define ICON_PATH_MAX 80

typedef struct {
    char path[ICON_PATH_MAX]; /* remote RA path, empty when the row has none */
    bool attempted;           /* set once, so failures are not retried forever */
} IconSlot;

typedef struct {
    IconSlot *slots;
    int       count;
} IconLoader;

/* Installs the loader on opts and enables the list's image column. */
void IconLoader_Attach(ap_list_opts *opts, IconLoader *loader);

/* Frees every texture the loader put on the items. */
void IconLoader_DestroyTextures(ap_list_item *items, int count);

#endif /* ICON_LOADER_H */
