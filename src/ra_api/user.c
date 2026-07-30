#include <stdio.h>

#include "cjson/cJSON.h"
#include "ra_api_internal.h"
#include "ra_api.h"

cJSON *RA_GetUserRecentAchievements(const char *user, int minutes) {
    char minutes_str[16];
    snprintf(minutes_str, sizeof(minutes_str), "%d", minutes);
    RA_Param params[] = {
        { "u", user },
        { "m", minutes_str },
    };
    cJSON *json = RA_GetRequest("GetUserRecentAchievements", params, 2);
    return json;
}

cJSON *RA_GetUserRecentlyPlayedGames(const char *user, int count) {
    if (count < 1) count = 1;
    if (count > RA_LIST_MAX) count = RA_LIST_MAX;
    char count_str[8];
    snprintf(count_str, sizeof(count_str), "%d", count);
    RA_Param params[] = {
        { "u", user },
        { "c", count_str },
    };
    cJSON *json = RA_GetRequest("GetUserRecentlyPlayedGames", params, 2);
    return json;
}

cJSON *RA_GetGameInfoAndUserProgress(const char *user, int game_id) {
    char game_id_str[RA_GAME_ID_LENGTH];
    snprintf(game_id_str, RA_GAME_ID_LENGTH, "%d", game_id);
    RA_Param params[] = {
        { "u", user },
        { "g", game_id_str },
        { "a", "1" },
    };
    cJSON *json = RA_GetRequest("GetGameInfoAndUserProgress", params,
                                sizeof(params) / sizeof(params[0]));
    return json;
}
