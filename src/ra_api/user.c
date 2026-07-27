#include <stdio.h>

#include "cjson/cJSON.h"
#include "ra_api_internal.h"
#include "ra_api.h"

cJSON *RA_GetUserRecentlyPlayedGames(const char *user) {
    RA_Param params[] = {
        { "u", user },
    };
    cJSON *json = RA_GetRequest("GetUserRecentlyPlayedGames", params, 1);
    return json;
}

cJSON *RA_GetGameInfoAndUserProgress(const char *user, int game_id) {
    char game_id_str[RA_GAME_ID_LENGTH];
    snprintf(game_id_str, RA_GAME_ID_LENGTH, "%d", game_id);
    RA_Param params[] = {
        { "u", user },
        { "g", game_id_str },
    };
    cJSON *json = RA_GetRequest("GetGameInfoAndUserProgress", params, 2);
    return json;
}