#ifndef RA_IMAGES_H
#define RA_IMAGES_H

#include <stdbool.h>
#include <stddef.h>

/* Resolves the on-disk path for a RetroAchievements image path downloading
   it on first use and reusing the cached copy afterwards.

   Writes the local path into out_path. Returns false if the image could not
   be made available locally, in which case out_path is unspecified. */
bool RA_GetImage(const char *ra_image_path, char *out_path, size_t out_size);

/* Local path of an already-cached image. Returns false when it is not cached.
   Never touches the network, so callers can test and load without risking a
   blocking download. */
bool RA_GetCachedImage(const char *ra_image_path, char *out_path, size_t out_size);

/* ── Concurrent, non-blocking downloads ──────────────────────────────────────
   Several images at once, advanced a slice at a time so a caller can drive
   them from a render loop without ever stalling a frame.

   Queue with RA_ImageFetchStart, advance with RA_ImageFetchPump every frame,
   and collect finished transfers by the tag they were queued with. Completed
   images are in the cache, so RA_GetCachedImage then resolves them. */

#define RA_IMAGE_FETCH_MAX 6 /* transfers in flight at once */

typedef struct RA_ImageFetcher RA_ImageFetcher;

RA_ImageFetcher *RA_ImageFetchCreate(void);

/* Cancels anything still running and releases the fetcher. */
void RA_ImageFetchDestroy(RA_ImageFetcher *fetcher);

/* Queues a download, reporting `tag` back when it finishes. Returns false when
   all slots are busy or the path is unusable. */
bool RA_ImageFetchStart(RA_ImageFetcher *fetcher, const char *ra_image_path, int tag);

/* Advances every transfer as far as it can go without blocking. Writes the
   tags that finished into out_tags and returns how many. */
int RA_ImageFetchPump(RA_ImageFetcher *fetcher, int *out_tags, int max_tags);

/* Blocks until everything queued has finished, for use before a screen is
   drawn, where there is no frame to protect. */
int RA_ImageFetchWait(RA_ImageFetcher *fetcher, int *out_tags, int max_tags);

/* Total bytes held by the image cache, and how many files that is. Either
   out parameter may be NULL. Returns false if the cache cannot be read. */
bool RA_GetImageCacheUsage(unsigned long long *out_bytes, int *out_files);

/* Deletes every cached image. Returns false if any file could not be removed. */
bool RA_ClearImageCache(void);

#endif /* RA_IMAGES_H */
