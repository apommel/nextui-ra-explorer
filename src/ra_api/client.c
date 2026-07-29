#define RA_API_BASE_URL "https://retroachievements.org/API/API_"
#define RA_INTERNAL_BASE_URL "https://retroachievements.org/internal-api/"
#define RA_API_URL_MAX 1024
#define RA_USER_AGENT "nextui-ra-explorer/1.0"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "cjson/cJSON.h"
#include "app/settings.h"
#include "ra_api_internal.h"

typedef struct {
    char *data;
    size_t size;
} CURL_Response;

static size_t CURL_WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t chunk_size = size * nmemb;
    CURL_Response *response = (CURL_Response *)userdata;

    char *new_data = realloc(response->data, response->size + chunk_size + 1);
    if (!new_data) {
        return 0;
    }

    response->data = new_data;
    memcpy(response->data + response->size, ptr, chunk_size);
    response->size += chunk_size;
    response->data[response->size] = '\0';

    return chunk_size;
}

/* Appends "&key=value" to url, URL-encoding both sides. Returns false if the
   encoding fails or the result would not fit. */
static bool CURL_AppendParam(char *url, size_t url_size, size_t *url_len, CURL *curl,
                           const RA_Param *param) {
    char *key = curl_easy_escape(curl, param->key, 0);
    char *value = curl_easy_escape(curl, param->value, 0);
    bool ok = false;

    if (key && value) {
        int written = snprintf(url + *url_len, url_size - *url_len, "&%s=%s", key, value);
        if (written > 0 && (size_t)written < url_size - *url_len) {
            *url_len += (size_t)written;
            ok = true;
        }
    }

    curl_free(key);
    curl_free(value);

    return ok;
}

void CURL_ApplyCommonOptions(CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, RA_USER_AGENT);

    const char *ca_bundle = getenv("SSL_CERT_FILE");
    if (ca_bundle && ca_bundle[0]) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
    }
}

/* Appends params to a URL that already carries its "?", fetches it and parses
   the body as JSON. Takes ownership of the handle. */
static cJSON *CURL_GetJson(CURL *curl, char *url, size_t url_size, size_t url_len,
                           const RA_Param *params, size_t param_count) {
    for (size_t i = 0; i < param_count; i++) {
        if (!CURL_AppendParam(url, url_size, &url_len, curl, &params[i])) {
            curl_easy_cleanup(curl);
            return NULL;
        }
    }

    CURL_Response response = { .data = malloc(1), .size = 0 };
    if (!response.data) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    response.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURL_ApplyCommonOptions(curl);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(response.data);
        return NULL;
    }

    cJSON *json = cJSON_Parse(response.data);
    free(response.data);

    return json;
}

cJSON *RA_GetRequest(const char *endpoint, const RA_Param *params, size_t param_count) {
    const Settings *settings = Settings_Get();
    if (!settings->api_key[0]) {
        return NULL; /* not configured yet */
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    /* curl_easy_escape needs the handle, so the URL is built after init. */
    char *api_key = curl_easy_escape(curl, settings->api_key, 0);
    if (!api_key) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    char url[RA_API_URL_MAX];
    int written = snprintf(url, sizeof(url), "%s%s.php?y=%s",
                           RA_API_BASE_URL, endpoint, api_key);
    curl_free(api_key);

    if (written < 0 || (size_t)written >= sizeof(url)) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    return CURL_GetJson(curl, url, sizeof(url), (size_t)written, params, param_count);
}

cJSON *RA_GetInternalRequest(const char *path, const RA_Param *params, size_t param_count) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    /* No API key: this is the website's own endpoint, not the public API. */
    char url[RA_API_URL_MAX];
    int written = snprintf(url, sizeof(url), "%s%s?", RA_INTERNAL_BASE_URL, path);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    return CURL_GetJson(curl, url, sizeof(url), (size_t)written, params, param_count);
}
