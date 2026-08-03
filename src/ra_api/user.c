#include <stdio.h>

#include "cjson/cJSON.h"
#include "ra_api_internal.h"
#include "ra_api.h"

/* How long a cached response is served without touching the network, per
   endpoint by how quickly its data changes. Past its TTL an entry is still
   served if a live request then fails for lack of a connection. */
#define RA_PROFILE_CACHE_TTL_SECONDS (60 * 60)              /* stats barely move */
#define RA_RECENT_ACHIEVEMENTS_CACHE_TTL_SECONDS (5 * 60)   /* about what is new */
#define RA_RECENTLY_PLAYED_CACHE_TTL_SECONDS (5 * 60)       /* same */
#define RA_GAME_PROGRESS_CACHE_TTL_SECONDS (15 * 60)        /* static metadata plus progress */

cJSON *RA_GetUserProfile(const char *user) {
    RA_Param params[] = {
        { "u", user },
    };
    return RA_GetRequest("GetUserProfile", params, 1, RA_PROFILE_CACHE_TTL_SECONDS);
}

cJSON *RA_VerifyUserProfile(const char *user) {
    RA_Param params[] = {
        { "u", user },
    };
    /* Uncached: a copy saved under the previous key would confirm a key that
       no longer applies. */
    return RA_GetRequest("GetUserProfile", params, 1, RA_CACHE_BYPASS);
}

cJSON *RA_GetUserRecentAchievements(const char *user, int minutes) {
    char minutes_str[16];
    snprintf(minutes_str, sizeof(minutes_str), "%d", minutes);
    RA_Param params[] = {
        { "u", user },
        { "m", minutes_str },
    };
    return RA_GetRequest("GetUserRecentAchievements", params, 2,
                         RA_RECENT_ACHIEVEMENTS_CACHE_TTL_SECONDS);
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
    return RA_GetRequest("GetUserRecentlyPlayedGames", params, 2,
                         RA_RECENTLY_PLAYED_CACHE_TTL_SECONDS);
}

cJSON *RA_GetGameInfoAndUserProgress(const char *user, int game_id) {
    char game_id_str[RA_GAME_ID_LENGTH];
    snprintf(game_id_str, RA_GAME_ID_LENGTH, "%d", game_id);
    RA_Param params[] = {
        { "u", user },
        { "g", game_id_str },
        { "a", "1" },
    };
    return RA_GetRequest("GetGameInfoAndUserProgress", params,
                         sizeof(params) / sizeof(params[0]),
                         RA_GAME_PROGRESS_CACHE_TTL_SECONDS);
}
