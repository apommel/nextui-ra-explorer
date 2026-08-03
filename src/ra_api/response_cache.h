#ifndef RA_RESPONSE_CACHE_H
#define RA_RESPONSE_CACHE_H

#include <stdbool.h>

#include "cjson/cJSON.h"

/* On-disk cache for parsed API responses, one JSON file per key, keyed by an
   opaque string the caller derives from the request. Separate from the image
   cache in images.c. */

/* Longer keys are truncated, which would let two requests share an entry. */
#define RA_CACHE_KEY_MAX 256

/* A max_age_seconds that accepts an entry however old it is. */
#define RA_CACHE_ANY_AGE (-1)

/* Returns the cached response for `key`, or NULL if there is none or it is
   older than max_age_seconds. The result is owned by the caller (free with
   cJSON_Delete).

   out_age_seconds, when not NULL, receives how old the entry is. */
cJSON *RA_CacheGet(const char *key, int max_age_seconds, int *out_age_seconds);

/* Same as RA_CacheGet but at any age, for when a live request has failed and
   previously seen data beats none. NULL if nothing was ever cached. */
cJSON *RA_CacheGetStale(const char *key, int *out_age_seconds);

/* Stores `json` under `key`, overwriting any previous entry. json is not
   consumed; the caller retains ownership. */
void RA_CacheSet(const char *key, const cJSON *json);

/* Total bytes held by the response cache, and how many files that is. Either
   out parameter may be NULL. Returns false if the cache cannot be read. */
bool RA_GetResponseCacheUsage(unsigned long long *out_bytes, int *out_files);

/* Deletes every cached response. Returns false if any file could not be removed. */
bool RA_ClearResponseCache(void);

#endif /* RA_RESPONSE_CACHE_H */
