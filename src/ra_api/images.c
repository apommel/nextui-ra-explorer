#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

#include "ra_api/images.h"
#include "util/paths.h"

#define RA_MEDIA_BASE_URL "https://media.retroachievements.org"

static bool RA_DownloadImage(const char *url, const char *dest_path) {
    /* Download to a temporary name and rename on success, so an interrupted
       transfer never leaves a truncated file behind to be cached forever. */
    char partial_path[600];
    if (snprintf(partial_path, sizeof(partial_path), "%s.part", dest_path) >= (int)sizeof(partial_path)) {
        return false;
    }

    FILE *file = fopen(partial_path, "wb");
    if (!file) {
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(file);
        remove(partial_path);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); /* treat 404 etc. as an error */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nextui-ra-explorer/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(file);

    if (res != CURLE_OK || rename(partial_path, dest_path) != 0) {
        remove(partial_path);
        return false;
    }

    return true;
}

/* Maps a RetroAchievements image path to where it lives (or would live) in the
   local cache. Touches no network and creates nothing. */
static bool RA_ImageCachePath(const char *ra_image_path, char *out_path, size_t out_size) {
    if (!ra_image_path || !ra_image_path[0]) {
        return false;
    }

    /* "/Images/067895.png" -> "067895.png" */
    const char *filename = strrchr(ra_image_path, '/');
    filename = filename ? filename + 1 : ra_image_path;
    if (!filename[0]) {
        return false;
    }

    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), "images")) {
        return false;
    }

    return snprintf(out_path, out_size, "%s/%s", dir, filename) < (int)out_size;
}

bool RA_IsImageCached(const char *ra_image_path) {
    char path[512];
    return RA_ImageCachePath(ra_image_path, path, sizeof(path)) && access(path, F_OK) == 0;
}

bool RA_GetImage(const char *ra_image_path, char *out_path, size_t out_size) {
    if (!RA_ImageCachePath(ra_image_path, out_path, out_size)) {
        return false;
    }

    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), "images")) {
        return false;
    }

    if (access(out_path, F_OK) == 0) {
        return true; /* already cached */
    }

    if (!Paths_MakeDirs(dir)) {
        return false;
    }

    char url[768];
    if (snprintf(url, sizeof(url), "%s%s", RA_MEDIA_BASE_URL, ra_image_path) >= (int)sizeof(url)) {
        return false;
    }

    return RA_DownloadImage(url, out_path);
}

/* Walks the cache directory once, accumulating sizes and optionally deleting.
   A few hundred small files, so a single pass is cheap enough to run on open. */
static bool RA_WalkImageCache(unsigned long long *out_bytes, int *out_files, bool remove_files) {
    if (out_bytes) *out_bytes = 0;
    if (out_files) *out_files = 0;

    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), "images")) {
        return false;
    }

    DIR *handle = opendir(dir);
    if (!handle) {
        return errno == ENOENT; /* nothing cached yet is a valid empty cache */
    }

    bool ok = true;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[512];
        if (snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) >= (int)sizeof(path)) {
            ok = false;
            continue;
        }

        struct stat info;
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;

        if (remove_files) {
            if (unlink(path) != 0) ok = false;
        } else {
            if (out_bytes) *out_bytes += (unsigned long long)info.st_size;
            if (out_files) (*out_files)++;
        }
    }

    closedir(handle);
    return ok;
}

bool RA_GetImageCacheUsage(unsigned long long *out_bytes, int *out_files) {
    return RA_WalkImageCache(out_bytes, out_files, false);
}

bool RA_ClearImageCache(void) {
    return RA_WalkImageCache(NULL, NULL, true);
}
