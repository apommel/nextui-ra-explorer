#include <stdio.h>

#include "cjson/cJSON.h"
#include "ra_api_internal.h"
#include "ra_api.h"

cJSON *RA_SearchGames(const char *query, int count) {
    if (count < 1) count = 1;
    if (count > RA_GAME_LIST_MAX) count = RA_GAME_LIST_MAX;
    char count_str[8];
    snprintf(count_str, sizeof(count_str), "%d", count);

    /* Without perPage the endpoint returns only 10. */
    RA_Param params[] = {
        { "q", query },
        { "scope", "games" },
        { "page", "1" },
        { "perPage", count_str },
    };
    cJSON *json = RA_GetInternalRequest("search", params,
                                        sizeof(params) / sizeof(params[0]));
    if (!json) {
        return NULL;
    }

    /* Hand back just the games array; the envelope carries nothing else we
       use. Detached so it outlives the response it came from. */
    cJSON *results = cJSON_GetObjectItemCaseSensitive(json, "results");
    cJSON *games = cJSON_DetachItemFromObjectCaseSensitive(results, "games");
    cJSON_Delete(json);

    return games;
}
