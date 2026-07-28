#ifndef VIEWS_H
#define VIEWS_H

#include <stdbool.h>

#include "cjson/cJSON.h"

/* Blocking full-screen views. Each returns once the user backs out of it. */

/* Main application view. */
void MainView(void);

/* Modal message with a single OK button. */
void InfoView(const char *message);

/* Username and API key editor; writes to settings.json on save. */
void SettingsView(void);

/* The signed-in user's recently played games; opens GameDetailView on select. */
void RecentGamesView(void);

/* The signed-in user's most recently unlocked achievements, across all games. */
void RecentAchievementsView(void);

/* Game info and the signed-in user's progress for one game. */
void GameDetailView(int game_id);

/* Achievements for one game, unlocked first. achievements is the "Achievements"
   object from a GetGameInfoAndUserProgress response; it is borrowed, not owned,
   and must outlive the call. */
void AchievementsListView(const char *game_title, cJSON *achievements);

/* One achievement, mapped from whichever endpoint supplied it, so the detail
   view needs no knowledge of the differing response shapes.

   Strings are borrowed from the source JSON and must outlive the view. Optional
   fields use NULL, and the award counts use -1, to mean "this source does not
   provide it" as distinct from a real zero. */
typedef struct {
    const char *title;
    const char *description;
    const char *badge_name;
    const char *author;
    const char *type;         /* NULL for an ordinary achievement */
    const char *game_title;   /* NULL when the source is already one game */
    const char *date_earned;  /* NULL when still locked */
    bool        hardcore;
    int         points;
    int         true_ratio;
    int         num_awarded;           /* -1 when not provided */
    int         num_awarded_hardcore;  /* -1 when not provided */
} Achievement;

/* One achievement's details. */
void AchievementDetailView(const Achievement *achievement);

#endif /* VIEWS_H */
