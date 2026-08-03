#ifndef RA_API_INTERNAL_H
#define RA_API_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <curl/curl.h>

#include "cjson/cJSON.h"

/* A single query string parameter, e.g. { "g", "14402" } -> "&g=14402".
   Both key and value are URL-encoded before being appended. */
typedef struct {
    const char *key;
    const char *value;
} RA_Param;

/* A cache_ttl_seconds that keeps a request off the cache altogether: not read
   from, not written to, and not fallen back on when it fails. */
#define RA_CACHE_BYPASS (-1)

/* Applies the options every request shares, including the CA bundle. On the
   device curl is statically linked against our own OpenSSL, so there is no
   system trust store: SSL_CERT_FILE must point at a bundle or every HTTPS
   request fails verification. Desktop builds use the system store and ignore it. */
void CURL_ApplyCommonOptions(CURL *curl);

/* Whether the device has a usable network link: any non-loopback interface
   that is up, running and holds an IPv4 address. Costs microseconds, so it is
   checked per request rather than remembered.

   Only ever proves the device is offline: an access point with no route out
   still reports a link, so a request that passes this can still time out. */
bool RA_HasNetworkLink(void);

/* Performs a GET request against the RetroAchievements API endpoint
   (the part after "API_", without the ".php" suffix) and parses the response.

   params may be NULL when param_count is 0. Authentication parameters are
   added automatically.

   cache_ttl_seconds enables the on-disk response cache, or RA_CACHE_BYPASS
   keeps this request off it entirely.

   Returns a cJSON object owned by the caller (free with cJSON_Delete), or
   NULL on failure. */
cJSON *RA_GetRequest(const char *endpoint, const RA_Param *params, size_t param_count,
                     int cache_ttl_seconds);

/* Performs a GET against retroachievements.org/internal-api/<path>.

   This is the website's own endpoint, not the documented public API: it takes
   no API key, and it carries no compatibility promise — the shape or the path
   may change without notice. Only use it where the public API has no
   equivalent, and let callers degrade gracefully when it returns NULL. */
cJSON *RA_GetInternalRequest(const char *path, const RA_Param *params, size_t param_count);

#endif /* RA_API_INTERNAL_H */
