#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

bool RA_GetImage(const char *ra_image_path, char *out_path, size_t out_size) {
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
    if (snprintf(out_path, out_size, "%s/%s", dir, filename) >= (int)out_size) {
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
