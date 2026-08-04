#define RA_API_BASE_URL "https://retroachievements.org/API/API_"
#define RA_INTERNAL_BASE_URL "https://retroachievements.org/internal-api/"
#define RA_API_URL_MAX 1024
#define RA_USER_AGENT "nextui-ra-explorer/1.1.0"

#define RA_CONNECT_TIMEOUT_SECONDS 5L

/* A transfer moving less than this for this long has stalled. */
#define RA_MIN_BYTES_PER_SECOND 100L
#define RA_MIN_SPEED_WINDOW_SECONDS 10L

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <curl/curl.h>

#include "cjson/cJSON.h"
#include "app/settings.h"
#include "ra_api.h"
#include "ra_api_internal.h"
#include "ra_api/response_cache.h"

typedef struct {
    char *data;
    size_t size;
} CURL_Response;

static RA_Error g_last_error = RA_ERROR_NONE;
static RA_Source g_last_source = RA_SOURCE_NETWORK;
static int g_last_age_seconds = -1;

RA_Error RA_GetLastError(void) {
    return g_last_error;
}

RA_Source RA_LastResponseSource(void) {
    return g_last_source;
}

int RA_LastResponseAgeSeconds(void) {
    return g_last_age_seconds;
}

/* Builds a cache key from the endpoint and its parameters. The API key never
   enters it: params never carry it, and it is appended to the URL later. */
static bool RA_BuildCacheKey(char *out, size_t out_size, const char *endpoint,
                             const RA_Param *params, size_t param_count) {
    int written = snprintf(out, out_size, "%s", endpoint);
    if (written < 0 || (size_t)written >= out_size) return false;
    size_t len = (size_t)written;

    for (size_t i = 0; i < param_count; i++) {
        written = snprintf(out + len, out_size - len, "_%s-%s",
                           params[i].key, params[i].value);
        if (written < 0 || (size_t)written >= out_size - len) return false;
        len += (size_t)written;
    }

    return true;
}

/* Falls back to the cache once the network has been ruled out. The request
   counts as successful from here, but returns NULL when nothing was ever
   cached for it, leaving the network error in place. */
static cJSON *RA_ServeStale(const char *cache_key) {
    int age = -1;
    cJSON *stale = RA_CacheGetStale(cache_key, &age);
    if (!stale) {
        return NULL;
    }

    g_last_source = RA_SOURCE_CACHE_OFFLINE;
    g_last_age_seconds = age;
    g_last_error = RA_ERROR_NONE;

    return stale;
}

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
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, RA_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, RA_MIN_BYTES_PER_SECOND);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, RA_MIN_SPEED_WINDOW_SECONDS);

    const char *ca_bundle = getenv("SSL_CERT_FILE");
    if (ca_bundle && ca_bundle[0]) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
    }
}

bool RA_HasNetworkLink(void) {
    struct ifaddrs *addrs;
    if (getifaddrs(&addrs) != 0) {
        return true;
    }

    bool linked = false;
    for (struct ifaddrs *ifa = addrs; ifa && !linked; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        /* UP is the configured state, RUNNING the actual one: wifi that is
           enabled but associated with nothing has the first without the
           second. Both, plus an address, means DHCP has completed. */
        linked = (ifa->ifa_flags & IFF_UP) && (ifa->ifa_flags & IFF_RUNNING);
    }

    freeifaddrs(addrs);
    return linked;
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

    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        /* No usable response at all — DNS, connection or timeout. */
        g_last_error = RA_ERROR_NETWORK;
        free(response.data);
        return NULL;
    }

    /* A rejected key still returns a well-formed JSON body, so the status has
       to be checked or the error document is mistaken for a result. */
    if (http_status == 401 || http_status == 403) {
        g_last_error = RA_ERROR_UNAUTHORIZED;
        free(response.data);
        return NULL;
    }
    if (http_status >= 400) {
        g_last_error = RA_ERROR_OTHER;
        free(response.data);
        return NULL;
    }

    cJSON *json = cJSON_Parse(response.data);
    free(response.data);

    if (!json) g_last_error = RA_ERROR_OTHER;
    return json;
}

cJSON *RA_GetRequest(const char *endpoint, const RA_Param *params, size_t param_count,
                     int cache_ttl_seconds) {
    g_last_error = RA_ERROR_NONE;
    g_last_source = RA_SOURCE_NETWORK;
    g_last_age_seconds = -1;

    const Settings *settings = Settings_Get();
    if (!settings->api_key[0]) {
        g_last_error = RA_ERROR_UNAUTHORIZED; /* not configured yet */
        return NULL;
    }

    char cache_key[RA_CACHE_KEY_MAX];
    bool have_cache_key = cache_ttl_seconds >= 0 &&
        RA_BuildCacheKey(cache_key, sizeof(cache_key), endpoint, params, param_count);

    /* Settled before the cache is read: a hit says nothing about connectivity,
       and one made offline must be reported as such however fresh it is. */
    bool linked = RA_HasNetworkLink();

    if (have_cache_key) {
        int age = -1;
        cJSON *cached = RA_CacheGet(cache_key, cache_ttl_seconds, &age);
        if (cached) {
            g_last_source = linked ? RA_SOURCE_CACHE : RA_SOURCE_CACHE_OFFLINE;
            g_last_age_seconds = age;
            return cached;
        }
    }

    if (!linked) {
        g_last_error = RA_ERROR_NETWORK;
        return have_cache_key ? RA_ServeStale(cache_key) : NULL;
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

    cJSON *json = CURL_GetJson(curl, url, sizeof(url), (size_t)written, params, param_count);
    if (json) {
        if (have_cache_key) RA_CacheSet(cache_key, json);
        return json;
    }

    /* Unreachable rather than rejected: prefer old data to an empty screen. */
    if (have_cache_key && g_last_error == RA_ERROR_NETWORK) {
        return RA_ServeStale(cache_key);
    }

    return NULL;
}

cJSON *RA_GetInternalRequest(const char *path, const RA_Param *params, size_t param_count) {
    g_last_error = RA_ERROR_NONE;
    g_last_source = RA_SOURCE_NETWORK;
    g_last_age_seconds = -1;

    /* Never cached, so with no link there is nothing to fall back on. */
    if (!RA_HasNetworkLink()) {
        g_last_error = RA_ERROR_NETWORK;
        return NULL;
    }

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
