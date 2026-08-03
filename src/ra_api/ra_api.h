#ifndef RA_API_H
#define RA_API_H

#define RA_GAME_ID_LENGTH 16

/* Rows shown in any list. Sources that support a server-side limit are
   asked for this many; the rest are truncated on arrival. */
#define RA_LIST_MAX 50

#include <stdbool.h>

#include "cjson/cJSON.h"

/* Why the last request failed. A rejected key and an unreachable server need
   different advice, and the API distinguishes them clearly: bad credentials
   come back as HTTP 401, while no connection fails before any response. */
typedef enum {
    RA_ERROR_NONE = 0,
    RA_ERROR_NETWORK,       /* server unreachable, timed out, DNS failure */
    RA_ERROR_UNAUTHORIZED,  /* API key missing or rejected */
    RA_ERROR_OTHER,
} RA_Error;

/* The failure behind the most recent NULL from any RA_ call. */
RA_Error RA_GetLastError(void);

/* Where the data behind the last successful RA_Get* call came from. A cache
   hit alone does not mean the app is offline: the cache also answers ordinary
   navigation on a healthy connection, and only the third case is worth
   reporting on screen. */
typedef enum {
    RA_SOURCE_NETWORK = 0,    /* fetched live */
    RA_SOURCE_CACHE,          /* cached and still fresh, with a link available */
    RA_SOURCE_CACHE_OFFLINE,  /* cached, and read because the server was out of reach */
} RA_Source;

/* Only meaningful right after a call that returned non-NULL. */
RA_Source RA_LastResponseSource(void);

/* How old the cached data behind the last such call is, in seconds, or -1
   when it came from the network. */
int RA_LastResponseAgeSeconds(void);

cJSON *RA_GetUserProfile(const char *user);

/* The same profile, always fetched live. Use this to confirm credentials the
   user has just entered: RA_GetUserProfile may answer from a copy saved under
   the previous key, which proves nothing about the new one. */
cJSON *RA_VerifyUserProfile(const char *user);
cJSON *RA_GetUserRecentAchievements(const char *user, int minutes);
cJSON *RA_GetUserRecentlyPlayedGames(const char *user, int count);
cJSON *RA_GetGameInfoAndUserProgress(const char *user, int game_id);

/* Searches games by title, returning the array of matches (caller owns it) or
   NULL. Backed by the website's internal endpoint rather than the public API,
   which has no search — see RA_GetInternalRequest for the caveats. */
cJSON *RA_SearchGames(const char *query, int count);

#endif /* RA_API_H */
