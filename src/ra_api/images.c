#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

#include "ra_api/images.h"
#include "ra_api/ra_api_internal.h"
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
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); /* treat 404 etc. as an error */
    CURL_ApplyCommonOptions(curl);

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

    /* Absolute URLs arrive from the search endpoint; keep only the path. */
    const char *path = ra_image_path;
    const char *scheme = strstr(path, "://");
    if (scheme) {
        path = strchr(scheme + 3, '/');
        if (!path) return false;
    }

    while (*path == '/') path++;
    if (!*path) {
        return false;
    }

    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), "images")) {
        return false;
    }

    int written = snprintf(out_path, out_size, "%s/%s", dir, path);
    if (written < 0 || (size_t)written >= out_size) {
        return false;
    }

    /* Fold the remote directories into the filename, leaving the cache dir
       itself untouched. */
    for (char *c = out_path + strlen(dir) + 1; *c; c++) {
        if (*c == '/') *c = '_';
    }

    return true;
}

bool RA_GetCachedImage(const char *ra_image_path, char *out_path, size_t out_size) {
    return RA_ImageCachePath(ra_image_path, out_path, out_size) &&
           access(out_path, F_OK) == 0;
}

/* Builds the remote URL for an image path. Most endpoints give a path like
   "/Images/067895.png", but some give the absolute URL; take those as-is. */
static bool RA_ImageUrl(const char *ra_image_path, char *out_url, size_t out_size) {
    bool absolute = strncmp(ra_image_path, "http://", 7) == 0 ||
                    strncmp(ra_image_path, "https://", 8) == 0;
    int written = absolute
        ? snprintf(out_url, out_size, "%s", ra_image_path)
        : snprintf(out_url, out_size, "%s%s", RA_MEDIA_BASE_URL, ra_image_path);

    return written > 0 && (size_t)written < out_size;
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
    if (!RA_ImageUrl(ra_image_path, url, sizeof(url))) {
        return false;
    }

    return RA_DownloadImage(url, out_path);
}

/* ── Concurrent, non-blocking downloads ───────────────────────────────────────
   One curl multi handle driving up to RA_IMAGE_FETCH_MAX transfers. The caller
   pumps it once per frame; curl_multi_perform advances whatever it can and
   returns immediately, so no frame ever waits on the network. */

typedef struct {
    CURL *easy;
    FILE *file;
    char  dest_path[512];
    char  part_path[520];
    int   tag;
    bool  in_use;
} RA_ImageTransfer;

struct RA_ImageFetcher {
    CURLM *multi;
    RA_ImageTransfer transfers[RA_IMAGE_FETCH_MAX];
};

RA_ImageFetcher *RA_ImageFetchCreate(void) {
    RA_ImageFetcher *fetcher = calloc(1, sizeof(*fetcher));
    if (!fetcher) {
        return NULL;
    }

    fetcher->multi = curl_multi_init();
    if (!fetcher->multi) {
        free(fetcher);
        return NULL;
    }

    return fetcher;
}

/* Detaches a transfer and closes its file. The partial download is kept only
   when the transfer succeeded, so a cancelled one leaves nothing behind. */
static void RA_ImageFetchRelease(RA_ImageFetcher *fetcher, RA_ImageTransfer *transfer,
                                 bool succeeded) {
    curl_multi_remove_handle(fetcher->multi, transfer->easy);
    curl_easy_cleanup(transfer->easy);
    fclose(transfer->file);

    if (!succeeded || rename(transfer->part_path, transfer->dest_path) != 0) {
        remove(transfer->part_path);
    }

    transfer->easy = NULL;
    transfer->file = NULL;
    transfer->in_use = false;
}

void RA_ImageFetchDestroy(RA_ImageFetcher *fetcher) {
    if (!fetcher) {
        return;
    }

    for (int i = 0; i < RA_IMAGE_FETCH_MAX; i++) {
        if (fetcher->transfers[i].in_use) {
            RA_ImageFetchRelease(fetcher, &fetcher->transfers[i], false);
        }
    }

    curl_multi_cleanup(fetcher->multi);
    free(fetcher);
}

bool RA_ImageFetchStart(RA_ImageFetcher *fetcher, const char *ra_image_path, int tag) {
    if (!fetcher) {
        return false;
    }

    RA_ImageTransfer *transfer = NULL;
    for (int i = 0; i < RA_IMAGE_FETCH_MAX; i++) {
        if (!fetcher->transfers[i].in_use) {
            transfer = &fetcher->transfers[i];
            break;
        }
    }
    if (!transfer) {
        return false; /* all slots busy — the caller retries next frame */
    }

    char dest[sizeof(transfer->dest_path)];
    char url[768];
    if (!RA_ImageCachePath(ra_image_path, dest, sizeof(dest)) ||
        !RA_ImageUrl(ra_image_path, url, sizeof(url))) {
        return false;
    }

    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), "images") || !Paths_MakeDirs(dir)) {
        return false;
    }

    if (snprintf(transfer->part_path, sizeof(transfer->part_path), "%s.part", dest)
            >= (int)sizeof(transfer->part_path)) {
        return false;
    }
    snprintf(transfer->dest_path, sizeof(transfer->dest_path), "%s", dest);

    transfer->file = fopen(transfer->part_path, "wb");
    if (!transfer->file) {
        return false;
    }

    transfer->easy = curl_easy_init();
    if (!transfer->easy) {
        fclose(transfer->file);
        remove(transfer->part_path);
        return false;
    }

    curl_easy_setopt(transfer->easy, CURLOPT_URL, url);
    curl_easy_setopt(transfer->easy, CURLOPT_WRITEDATA, transfer->file);
    curl_easy_setopt(transfer->easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(transfer->easy, CURLOPT_PRIVATE, transfer);
    CURL_ApplyCommonOptions(transfer->easy);

    if (curl_multi_add_handle(fetcher->multi, transfer->easy) != CURLM_OK) {
        curl_easy_cleanup(transfer->easy);
        fclose(transfer->file);
        remove(transfer->part_path);
        transfer->easy = NULL;
        transfer->file = NULL;
        return false;
    }

    transfer->tag = tag;
    transfer->in_use = true;

    return true;
}

/* Harvests whatever curl has finished since the last call. */
static int RA_ImageFetchCollect(RA_ImageFetcher *fetcher, int *out_tags, int max_tags) {
    int found = 0;
    int queued = 0;
    CURLMsg *msg;

    while ((msg = curl_multi_info_read(fetcher->multi, &queued)) != NULL) {
        if (msg->msg != CURLMSG_DONE) continue;

        RA_ImageTransfer *transfer = NULL;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&transfer);
        if (!transfer) continue;

        int tag = transfer->tag;
        RA_ImageFetchRelease(fetcher, transfer, msg->data.result == CURLE_OK);

        if (msg->data.result == CURLE_OK && found < max_tags) {
            out_tags[found++] = tag;
        }
    }

    return found;
}

int RA_ImageFetchPump(RA_ImageFetcher *fetcher, int *out_tags, int max_tags) {
    if (!fetcher) {
        return 0;
    }

    int running = 0;
    curl_multi_perform(fetcher->multi, &running);

    return RA_ImageFetchCollect(fetcher, out_tags, max_tags);
}

int RA_ImageFetchWait(RA_ImageFetcher *fetcher, int *out_tags, int max_tags) {
    if (!fetcher) {
        return 0;
    }

    int found = 0;
    int running = 0;

    do {
        curl_multi_perform(fetcher->multi, &running);
        found += RA_ImageFetchCollect(fetcher, out_tags + found, max_tags - found);

        /* Sleep until there is something to do rather than spinning. */
        if (running > 0) curl_multi_poll(fetcher->multi, NULL, 0, 100, NULL);
    } while (running > 0 && found < max_tags);

    return found;
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
