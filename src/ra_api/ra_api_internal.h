#ifndef RA_API_INTERNAL_H
#define RA_API_INTERNAL_H

#include <stddef.h>
#include "cjson/cJSON.h"

/* A single query string parameter, e.g. { "g", "14402" } -> "&g=14402".
   Both key and value are URL-encoded before being appended. */
typedef struct {
    const char *key;
    const char *value;
} RA_Param;

/* Performs a GET request against the RetroAchievements API endpoint
   (the part after "API_", without the ".php" suffix) and parses the response.

   params may be NULL when param_count is 0. Authentication parameters are
   added automatically.

   Returns a cJSON object owned by the caller (free with cJSON_Delete), or
   NULL on failure. */
cJSON *RA_GetRequest(const char *endpoint, const RA_Param *params, size_t param_count);

#endif /* RA_API_INTERNAL_H */
