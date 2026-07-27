#ifndef RA_IMAGES_H
#define RA_IMAGES_H

#include <stdbool.h>
#include <stddef.h>

/* Resolves the on-disk path for a RetroAchievements image path downloading
   it on first use and reusing the cached copy afterwards.

   Writes the local path into out_path. Returns false if the image could not
   be made available locally, in which case out_path is unspecified. */
bool RA_GetImage(const char *ra_image_path, char *out_path, size_t out_size);

#endif /* RA_IMAGES_H */
