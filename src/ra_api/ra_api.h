#ifndef RA_API_H
#define RA_API_H

#define RA_GAME_ID_LENGTH 16

#include "cjson/cJSON.h"

cJSON *RA_GetUserRecentAchievements(const char *user, int minutes);
cJSON *RA_GetUserRecentlyPlayedGames(const char *user, int count);
cJSON *RA_GetGameInfoAndUserProgress(const char *user, int game_id);

#endif /* RA_API_H */
