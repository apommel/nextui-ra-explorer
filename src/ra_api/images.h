#ifndef RA_IMAGES_H
#define RA_IMAGES_H

#include <stdbool.h>
#include <stddef.h>

/* Resolves the on-disk path for a RetroAchievements image path downloading
   it on first use and reusing the cached copy afterwards.

   Writes the local path into out_path. Returns false if the image could not
   be made available locally, in which case out_path is unspecified. */
bool RA_GetImage(const char *ra_image_path, char *out_path, size_t out_size);

/* True when the image is already cached, i.e. RA_GetImage would not hit the
   network. Cheap: one stat(). */
bool RA_IsImageCached(const char *ra_image_path);

/* Total bytes held by the image cache, and how many files that is. Either
   out parameter may be NULL. Returns false if the cache cannot be read. */
bool RA_GetImageCacheUsage(unsigned long long *out_bytes, int *out_files);

/* Deletes every cached image. Returns false if any file could not be removed. */
bool RA_ClearImageCache(void);

#endif /* RA_IMAGES_H */
