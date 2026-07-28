#ifndef RA_API_H
#define RA_API_H

#define RA_GAME_ID_LENGTH 16

/* Rows fetched for a game list. Both sources cap at 50. */
#define RA_GAME_LIST_MAX 50

#include "cjson/cJSON.h"

cJSON *RA_GetUserRecentAchievements(const char *user, int minutes);
cJSON *RA_GetUserRecentlyPlayedGames(const char *user, int count);
cJSON *RA_GetGameInfoAndUserProgress(const char *user, int game_id);

/* Searches games by title, returning the array of matches (caller owns it) or
   NULL. Backed by the website's internal endpoint rather than the public API,
   which has no search — see RA_GetInternalRequest for the caveats. */
cJSON *RA_SearchGames(const char *query, int count);

#endif /* RA_API_H */
