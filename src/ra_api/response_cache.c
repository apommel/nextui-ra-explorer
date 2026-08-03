#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "cjson/cJSON.h"
#include "ra_api/response_cache.h"
#include "util/file.h"
#include "util/paths.h"

#define RA_CACHE_SUBDIR "responses"

/* Maps a cache key to its file path, folding anything not filesystem-safe
   into '_' so callers can build keys straight from endpoints and values. */
static bool RA_CacheKeyToPath(const char *key, char *out_path, size_t out_size) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), RA_CACHE_SUBDIR)) {
        return false;
    }

    char safe_key[RA_CACHE_KEY_MAX];
    size_t i = 0;
    for (const char *p = key; *p && i + 1 < sizeof(safe_key); p++) {
        unsigned char c = (unsigned char)*p;
        safe_key[i++] = (isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    }
    safe_key[i] = '\0';

    int written = snprintf(out_path, out_size, "%s/%s.json", dir, safe_key);
    return written > 0 && (size_t)written < out_size;
}

cJSON *RA_CacheGet(const char *key, int max_age_seconds, int *out_age_seconds) {
    if (out_age_seconds) *out_age_seconds = -1;

    char path[512];
    if (!RA_CacheKeyToPath(key, path, sizeof(path))) {
        return NULL;
    }

    struct stat info;
    if (stat(path, &info) != 0) {
        return NULL;
    }

    /* These devices have no battery-backed clock and boot in the past, which
       dates an entry into the future. Treat that as brand new. */
    double age = difftime(time(NULL), info.st_mtime);
    if (age < 0) age = 0;

    if (max_age_seconds >= 0 && age > max_age_seconds) {
        return NULL;
    }

    char *text = File_ReadAll(path);
    if (!text) {
        return NULL;
    }

    cJSON *json = cJSON_Parse(text);
    free(text);

    if (json && out_age_seconds) *out_age_seconds = (int)age;

    return json;
}

cJSON *RA_CacheGetStale(const char *key, int *out_age_seconds) {
    return RA_CacheGet(key, RA_CACHE_ANY_AGE, out_age_seconds);
}

void RA_CacheSet(const char *key, const cJSON *json) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), RA_CACHE_SUBDIR) || !Paths_MakeDirs(dir)) {
        return;
    }

    char path[512];
    if (!RA_CacheKeyToPath(key, path, sizeof(path))) {
        return;
    }

    char *text = cJSON_PrintUnformatted(json);
    if (!text) {
        return;
    }

    /* Write to a temporary name and rename on success, so an interrupted write
       never leaves a truncated file behind to be read as a hit later. */
    char part_path[560];
    if (snprintf(part_path, sizeof(part_path), "%s.part", path) >= (int)sizeof(part_path)) {
        free(text);
        return;
    }

    FILE *file = fopen(part_path, "wb");
    if (!file) {
        free(text);
        return;
    }

    bool ok = fputs(text, file) >= 0;
    ok = (fclose(file) == 0) && ok;
    free(text);

    if (ok && rename(part_path, path) == 0) {
        return;
    }
    remove(part_path);
}

bool RA_GetResponseCacheUsage(unsigned long long *out_bytes, int *out_files) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), RA_CACHE_SUBDIR)) {
        return false;
    }

    return Paths_DirUsage(dir, out_bytes, out_files);
}

bool RA_ClearResponseCache(void) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), RA_CACHE_SUBDIR)) {
        return false;
    }

    return Paths_ClearDir(dir);
}
