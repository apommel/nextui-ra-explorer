#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "app/icon_loader.h"
#include "app/settings.h"
#include "app/views.h"
#include "ra_api/images.h"
#include "ra_api/ra_api.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Returns the string field, or "-" when it is missing, empty, or not a string.
   The result points into json and stays valid until json is deleted. */
static const char *GetStringValue(cJSON *json, const char *key) {
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, key));
    return (value && value[0]) ? value : "-";
}

/* Like GetStringValue, but reports a missing field as NULL rather than "-",
   for optional values the caller needs to distinguish. */
static const char *GetStringOrNull(cJSON *json, const char *key) {
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, key));
    return (value && value[0]) ? value : NULL;
}

/* Returns the numeric field, or 0 when it is missing or not a number.
   cJSON_GetNumberValue yields NaN for those, and casting NaN to int is UB. */
static int GetIntValue(cJSON *json, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsNumber(item) ? (int)cJSON_GetNumberValue(item) : 0;
}

/* ── Achievement mapping ─────────────────────────────────────────────────────
   The two endpoints that return achievements disagree on field names and on
   which fields exist at all, so each gets its own mapper and the detail view
   sees only the common shape. */

/* An entry of the "Achievements" object from GetGameInfoAndUserProgress. */
static Achievement AchievementFromGameProgress(cJSON *entry) {
    return (Achievement){
        .title                = GetStringOrNull(entry, "Title"),
        .description          = GetStringOrNull(entry, "Description"),
        .badge_name           = GetStringOrNull(entry, "BadgeName"),
        .author               = GetStringOrNull(entry, "Author"),
        .type                 = GetStringOrNull(entry, "Type"),
        .game_title           = NULL, /* the whole list is one game */
        /* No jump back to the game: it is already one B press away, and
           offering it here would let the two screens push each other forever. */
        .game_id              = 0,
        .date_earned          = GetStringOrNull(entry, "DateEarned"),
        /* DateEarnedHardcore is sent only on a hardcore unlock. */
        .hardcore             = GetStringOrNull(entry, "DateEarnedHardcore") != NULL,
        .points               = GetIntValue(entry, "Points"),
        .true_ratio           = GetIntValue(entry, "TrueRatio"),
        .num_awarded          = GetIntValue(entry, "NumAwarded"),
        .num_awarded_hardcore = GetIntValue(entry, "NumAwardedHardcore"),
    };
}

/* An element of the GetUserRecentAchievements array. */
static Achievement AchievementFromRecent(cJSON *entry) {
    /* One "Date" plus a HardcoreMode flag here, rather than two date fields. */
    cJSON *hardcore = cJSON_GetObjectItemCaseSensitive(entry, "HardcoreMode");

    return (Achievement){
        .title                = GetStringOrNull(entry, "Title"),
        .description          = GetStringOrNull(entry, "Description"),
        .badge_name           = GetStringOrNull(entry, "BadgeName"),
        .author               = GetStringOrNull(entry, "Author"),
        .type                 = GetStringOrNull(entry, "Type"),
        .game_title           = GetStringOrNull(entry, "GameTitle"),
        /* Reached from a cross-game list, so the game screen is worth offering. */
        .game_id              = GetIntValue(entry, "GameID"),
        .date_earned          = GetStringOrNull(entry, "Date"),
        .hardcore             = cJSON_IsTrue(hardcore) ||
                                (cJSON_IsNumber(hardcore) && cJSON_GetNumberValue(hardcore) != 0),
        .points               = GetIntValue(entry, "Points"),
        .true_ratio           = GetIntValue(entry, "TrueRatio"),
        /* This endpoint carries no award counts. */
        .num_awarded          = -1,
        .num_awarded_hardcore = -1,
    };
}

/* "win_condition" -> "Win Condition", "beaten-hardcore" -> "Beaten Hardcore".

   RA's machine-readable labels are snake_case for achievement Type but
   hyphenated for award kinds, so both separators are treated alike. The value
   sets are open-ended, so this transforms generically instead of mapping the
   ones we know about — an unrecognised value still renders sensibly. */
static void FormatSnakeCase(const char *value, char *out, size_t out_size) {
    size_t i = 0;
    bool word_start = true;

    for (const char *p = value; *p && i + 1 < out_size; p++) {
        if (*p == '_' || *p == '-') {
            out[i++] = ' ';
            word_start = true;
            continue;
        }

        out[i++] = word_start ? (char)toupper((unsigned char)*p) : *p;
        word_start = false;
    }

    out[i] = '\0';
}

/* "12.3 MB (418)", or "empty". */
static void FormatCacheUsage(char *out, size_t out_size) {
    unsigned long long bytes = 0;
    int files = 0;

    if (!RA_GetImageCacheUsage(&bytes, &files)) {
        snprintf(out, out_size, "unknown");
    } else if (files == 0) {
        snprintf(out, out_size, "empty");
    } else {
        snprintf(out, out_size, "%.1f MB (%d)", (double)bytes / (1024.0 * 1024.0), files);
    }
}

typedef struct {
    cJSON *achievement;
    int    id;
    int    display_order;
    bool   unlocked;
} AchievementEntry;

/* qsort() takes no context pointer, and qsort_r's signature is not portable,
   so the grouping choice is handed to the comparator through a file static. */
static bool g_sort_unlocked_first;

/* Everything is ordered by DisplayOrder; when g_sort_unlocked_first is set,
   unlocked achievements are grouped ahead of locked ones first. */
static int CompareAchievements(const void *a, const void *b) {
    const AchievementEntry *x = (const AchievementEntry *)a;
    const AchievementEntry *y = (const AchievementEntry *)b;

    if (g_sort_unlocked_first && x->unlocked != y->unlocked) {
        return x->unlocked ? -1 : 1;
    }

    if (x->display_order != y->display_order) {
        return (x->display_order > y->display_order) - (x->display_order < y->display_order);
    }

    /* DisplayOrder is not unique, and qsort is not stable; fall back to ID so
       the order stays the same between runs. */
    return (x->id > y->id) - (x->id < y->id);
}

/* ── Navigation ──────────────────────────────────────────────────────────────
*/

/* Reports a failed request, naming the cause where the API lets us tell them
   apart, so a wrong key does not read as "the server is down". */
static void RequestFailedView(const char *what) {
    const char *reason;
    switch (RA_GetLastError()) {
    case RA_ERROR_UNAUTHORIZED:
        reason = "Invalid API key. Check it in Settings.";
        break;
    case RA_ERROR_NETWORK:
        reason = "Could not reach RetroAchievements. Check the network connection.";
        break;
    default:
        reason = "RetroAchievements returned an unexpected response.";
        break;
    }

    char message[256];
    snprintf(message, sizeof(message), "%s %s", what, reason);
    InfoView(message);
}

/* Guards views that hit the public API, which needs credentials. Search is the
   exception — it goes through the website endpoint and takes no key — so a game
   reached from search results has to be checked here rather than at the list. */
static bool RequireSettings(void) {
    if (Settings_IsConfigured()) return true;
    InfoView("Set your username and API key in Settings first.");
    return false;
}

/* One row of a game list. All strings are borrowed and must outlive the call. */
typedef struct {
    int         game_id;
    const char *title;
    const char *icon_path;  /* remote RA path or URL; NULL for no icon */
} GameListRow;

static void OpenGameAchievements(int game_id);

/* The recently-played and search screens differ only in how they obtain their
   rows, so the list itself — icons, bindings, cleanup — lives here once. */
static void GameListView(const char *screen_title, const GameListRow *rows, int count) {
    ap_list_item *items = calloc((size_t)count, sizeof(*items));
    IconSlot *slots = calloc((size_t)count, sizeof(*slots));
    if (!items || !slots) {
        free(items);
        free(slots);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (rows[i].icon_path) {
            snprintf(slots[i].path, sizeof(slots[i].path), "%s", rows[i].icon_path);
        }
        items[i] = (ap_list_item){ .label = rows[i].title };
    }

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_Y, .label = "Achievements" },
        { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
    };

    IconLoader loader = { .slots = slots, .count = count };

    ap_list_opts opts = ap_list_default_opts(screen_title, items, count);
    opts.footer       = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);
    opts.secondary_action_button = AP_BTN_Y;
    IconLoader_Attach(&opts, &loader);

    /* A opens the game, Y skips straight to its achievements. */
    for (;;) {
        ap_list_result result;
        if (ap_list(&opts, &result) != AP_OK) break;
        if (result.selected_index < 0 || result.selected_index >= count) break;

        opts.initial_index       = result.selected_index;
        opts.visible_start_index = result.visible_start_index;

        int game_id = rows[result.selected_index].game_id;
        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            OpenGameAchievements(game_id);
        } else if (result.action == AP_ACTION_SELECTED) {
            GameDetailView(game_id);
        }
    }

    IconLoader_Release(&loader, items, count);
    free(items);
    free(slots);
}

/* One row of an achievement list. The Achievement is carried by value so the
   detail screen needs nothing else; strings inside it are still borrowed. */
typedef struct {
    Achievement achievement;
    char        icon_path[ICON_PATH_MAX];
} AchievementListRow;

/* Shared by the per-game and cross-game achievement lists, which differ only
   in how they build and order their rows. */
static void AchievementListView(const char *screen_title,
                                const AchievementListRow *rows, int count) {
    ap_list_item *items = calloc((size_t)count, sizeof(*items));
    IconSlot *slots = calloc((size_t)count, sizeof(*slots));
    char (*points)[16] = calloc((size_t)count, sizeof(*points));
    if (!items || !slots || !points) {
        free(items);
        free(slots);
        free(points);
        return;
    }

    for (int i = 0; i < count; i++) {
        const Achievement *a = &rows[i].achievement;

        snprintf(points[i], sizeof(points[i]), "%d pts", a->points);
        snprintf(slots[i].path, sizeof(slots[i].path), "%s", rows[i].icon_path);

        items[i] = (ap_list_item){
            .label         = a->title ? a->title : "-",
            .metadata      = a->description,
            .trailing_text = points[i],
        };
    }

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
    };

    IconLoader loader = { .slots = slots, .count = count };

    ap_list_opts opts = ap_list_default_opts(screen_title, items, count);
    opts.footer       = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);
    IconLoader_Attach(&opts, &loader);

    /* A opens one achievement and returns here; B leaves the list. */
    for (;;) {
        ap_list_result result;
        if (ap_list(&opts, &result) != AP_OK) break;
        if (result.selected_index < 0 || result.selected_index >= count) break;

        opts.initial_index       = result.selected_index;
        opts.visible_start_index = result.visible_start_index;

        AchievementDetailView(&rows[result.selected_index].achievement);
    }

    IconLoader_Release(&loader, items, count);
    free(items);
    free(slots);
    free(points);
}

/* Opens a game's achievements directly, skipping its detail screen. The list
   needs the per-game response, which only the game endpoint provides. */
static void OpenGameAchievements(int game_id) {
    if (!RequireSettings()) return;

    cJSON *json = RA_GetGameInfoAndUserProgress(Settings_Get()->username, game_id);

    cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "Title");
    if (!cJSON_IsString(title)) {
        cJSON_Delete(json);
        RequestFailedView("Could not load this game.");
        return;
    }

    AchievementsListView(cJSON_GetStringValue(title),
                         cJSON_GetObjectItemCaseSensitive(json, "Achievements"));
    cJSON_Delete(json);
}

/* ── Views ───────────────────────────────────────────────────────────────── */

void MainView(void) {
    enum { MENU_RECENT_ACHIEVEMENTS, MENU_RECENT_GAMES, MENU_SEARCH_GAMES };

    ap_list_item items[] = {
        [MENU_RECENT_ACHIEVEMENTS] = { .label = "Recent Achievements" },
        [MENU_RECENT_GAMES]        = { .label = "Recently Played Games" },
        [MENU_SEARCH_GAMES]        = { .label = "Search Games" },
    };

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Quit" },
        { .button = AP_BTN_X, .label = "Settings" },
        { .button = AP_BTN_A, .label = "Select", .is_confirm = true },
    };

    ap_list_opts opts = ap_list_default_opts("RA Explorer", items,
                                             sizeof(items) / sizeof(items[0]));
    opts.footer       = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);
    opts.secondary_action_button = AP_BTN_X;

    /* Stay on the menu until the user backs out with B */
    for (;;) {
        ap_list_result result;
        if (ap_list(&opts, &result) != AP_OK) break;

        /* Come back with the cursor and scroll position the user left. */
        opts.initial_index       = result.selected_index;
        opts.visible_start_index = result.visible_start_index;

        if (result.action == AP_ACTION_SECONDARY_TRIGGERED) {
            SettingsView();
        } else if (result.action == AP_ACTION_SELECTED) {
            if (result.selected_index == MENU_RECENT_ACHIEVEMENTS) {
                RecentAchievementsView();
            }
            else if (result.selected_index == MENU_RECENT_GAMES) {
                RecentGamesView();
            }
            else if (result.selected_index == MENU_SEARCH_GAMES) {
                SearchGamesView();
            }
        }
    }
}

void InfoView(const char *message) {
    ap_footer_item footer[] = {
        { .button = AP_BTN_A, .label = "OK", .is_confirm = true },
    };
    ap_message_opts opts = {
        .message = message,
        .footer = footer, .footer_count = 1,
    };
    ap_confirm_result result;
    ap_confirmation(&opts, &result);
}

void SettingsView(void) {
    const Settings *settings = Settings_Get();

    /* ap_options_list free()s and replaces these when the keyboard is
       confirmed, so they must be heap allocated rather than literals. */
    ap_option username_option[] = {
        { .label = strdup(settings->username), .value = strdup(settings->username) },
    };
    ap_option api_key_option[] = {
        { .label = strdup(settings->api_key), .value = strdup(settings->api_key) },
    };

    /* Standard options are only ever indexed, never freed by the widget, so
       unlike the keyboard rows these can be literals. */
    ap_option unlocked_first_options[] = {
        { .label = "On",  .value = "On"  },
        { .label = "Off", .value = "Off" },
    };

    char cache_usage[48];
    FormatCacheUsage(cache_usage, sizeof(cache_usage));
    ap_option cache_option[] = {
        { .label = cache_usage, .value = cache_usage },
    };

    enum { ROW_USERNAME, ROW_API_KEY, ROW_UNLOCKED_FIRST, ROW_CLEAR_CACHE };

    ap_options_item items[] = {
        [ROW_USERNAME] = { .label = "Username", .type = AP_OPT_KEYBOARD,
          .options = username_option, .option_count = 1, .selected_option = 0 },
        [ROW_API_KEY] = { .label = "API Key", .type = AP_OPT_KEYBOARD,
          .options = api_key_option, .option_count = 1, .selected_option = 0 },
        [ROW_UNLOCKED_FIRST] = { .label = "Unlocked First", .type = AP_OPT_STANDARD,
          .options = unlocked_first_options,
          .option_count = sizeof(unlocked_first_options) / sizeof(unlocked_first_options[0]),
          .selected_option = settings->unlocked_first ? 0 : 1 },
        [ROW_CLEAR_CACHE] = { .label = "Clear Image Cache", .type = AP_OPT_CLICKABLE,
          .options = cache_option, .option_count = 1, .selected_option = 0 },
    };

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Cancel" },
        { .button = AP_BTN_A, .label = "Edit" },
        { .button = AP_BTN_START, .label = "Save", .is_confirm = true },
    };

    ap_options_list_opts opts = {
        .title = "Settings",
        .items = items,
        .item_count = sizeof(items) / sizeof(items[0]),
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .confirm_button = AP_BTN_START,
    };

    /* A clickable row exits the options list, so re-enter after handling it. */
    for (;;) {
        ap_options_list_result result;
        ap_options_list(&opts, &result);

        if (result.action == AP_ACTION_SELECTED && result.focused_index == ROW_CLEAR_CACHE) {
            if (!RA_ClearImageCache()) {
                InfoView("Some cached images could not be deleted.");
            }
            FormatCacheUsage(cache_usage, sizeof(cache_usage));

            opts.initial_selected_index = result.focused_index;
            opts.visible_start_index    = result.visible_start_index;
            continue;
        }

        if (result.action == AP_ACTION_CONFIRMED) {
            Settings updated = *settings;
            snprintf(updated.username, sizeof(updated.username), "%s", username_option[0].value);
            snprintf(updated.api_key, sizeof(updated.api_key), "%s", api_key_option[0].value);
            updated.unlocked_first = (items[ROW_UNLOCKED_FIRST].selected_option == 0);

            Settings_Set(&updated);
            if (!Settings_Save()) {
                InfoView("Failed to save settings.");
            }
        }
        break;
    }

    free((void *)username_option[0].label);
    free((void *)username_option[0].value);
    free((void *)api_key_option[0].label);
    free((void *)api_key_option[0].value);
}

void AchievementsListView(const char *game_title, cJSON *achievements) {
    int count = cJSON_GetArraySize(achievements);
    if (count <= 0) {
        InfoView("This game has no achievements.");
        return;
    }

    /* Heap rather than VLAs: a game can carry a few hundred achievements. */
    AchievementEntry *entries = calloc((size_t)count, sizeof(*entries));
    AchievementListRow *rows = calloc((size_t)count, sizeof(*rows));
    if (!entries || !rows) {
        free(entries);
        free(rows);
        return;
    }

    /* "Achievements" is an object keyed by achievement id, not an array. */
    cJSON *achievement;
    int i = 0;
    cJSON_ArrayForEach(achievement, achievements) {
        const char *date_earned = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(achievement, "DateEarned"));

        entries[i++] = (AchievementEntry){
            .achievement   = achievement,
            .id            = GetIntValue(achievement, "ID"),
            .display_order = GetIntValue(achievement, "DisplayOrder"),
            /* Locked achievements omit DateEarned; treat "" as locked too, in
               case the API ever returns an empty string instead. */
            .unlocked      = date_earned != NULL && date_earned[0] != '\0',
        };
    }

    g_sort_unlocked_first = Settings_Get()->unlocked_first;
    qsort(entries, (size_t)count, sizeof(*entries), CompareAchievements);

    for (i = 0; i < count; i++) {
        rows[i].achievement = AchievementFromGameProgress(entries[i].achievement);

        /* RA publishes a grayscale variant of every badge at "<name>_lock.png",
           so locked rows need no local image processing. */
        if (rows[i].achievement.badge_name) {
            snprintf(rows[i].icon_path, sizeof(rows[i].icon_path), "/Badge/%s%s.png",
                     rows[i].achievement.badge_name, entries[i].unlocked ? "" : "_lock");
        }
    }

    AchievementListView(game_title, rows, count);

    free(rows);
    free(entries);
}

void SearchGamesView(void) {
    ap_keyboard_result query;
    /* NULL keeps the built-in key help, which documents Y to cancel — B is
       backspace here, not back. */
    if (ap_keyboard("", NULL, AP_KB_GENERAL, &query) != AP_OK) {
        return;
    }
    if (!query.text[0]) {
        return;
    }

    cJSON *games = RA_SearchGames(query.text, RA_LIST_MAX);
    if (!games) {
        RequestFailedView("Search failed.");
        return;
    }

    int count = cJSON_GetArraySize(games);
    if (count <= 0) {
        cJSON_Delete(games);
        InfoView("No games matched that title.");
        return;
    }

    GameListRow *rows = calloc((size_t)count, sizeof(*rows));
    if (!rows) {
        cJSON_Delete(games);
        return;
    }

    cJSON *game;
    int i = 0;
    cJSON_ArrayForEach(game, games) {
        rows[i++] = (GameListRow){
            .game_id   = GetIntValue(game, "id"),
            .title     = GetStringValue(game, "title"),
            /* badgeUrl is absolute here, unlike the public API's bare paths;
               RA_GetImage takes either. */
            .icon_path = GetStringOrNull(game, "badgeUrl"),
        };
    }

    GameListView("Search Results", rows, count);

    free(rows);
    cJSON_Delete(games);
}

/* How far back "recent" reaches. The endpoint defaults to 60 minutes, which is
   narrow enough to come back empty for most players; six months is usually
   enough to fill a screen. It has no server-side limit, so the response is
   truncated to RA_LIST_MAX below. */
#define RECENT_ACHIEVEMENTS_MINUTES (182 * 24 * 60)

void RecentAchievementsView(void) {
    if (!RequireSettings()) return;

    cJSON *json = RA_GetUserRecentAchievements(Settings_Get()->username,
                                               RECENT_ACHIEVEMENTS_MINUTES);
    if (!json) {
        RequestFailedView("Could not load recent achievements.");
        return;
    }

    /* An empty array is a valid answer here, unlike a failed request. */
    int count = cJSON_GetArraySize(json);
    if (count <= 0) {
        cJSON_Delete(json);
        InfoView("No achievements unlocked in the past six months.");
        return;
    }

    /* Most recent first, so truncating keeps the newest. */
    if (count > RA_LIST_MAX) count = RA_LIST_MAX;

    AchievementListRow *rows = calloc((size_t)count, sizeof(*rows));
    if (!rows) {
        cJSON_Delete(json);
        return;
    }

    /* The API already returns most recent first, so the response order stands
       — there is no cross-game DisplayOrder to sort on. */
    cJSON *entry;
    int i = 0;
    cJSON_ArrayForEach(entry, json) {
        if (i >= count) break;

        rows[i].achievement = AchievementFromRecent(entry);

        /* Unlocked by definition, so always the colour badge. BadgeURL is
           already a full path; BadgeName is the fallback. */
        const char *badge_url = GetStringOrNull(entry, "BadgeURL");
        if (badge_url) {
            snprintf(rows[i].icon_path, sizeof(rows[i].icon_path), "%s", badge_url);
        } else if (rows[i].achievement.badge_name) {
            snprintf(rows[i].icon_path, sizeof(rows[i].icon_path), "/Badge/%s.png",
                     rows[i].achievement.badge_name);
        }
        i++;
    }

    AchievementListView("Recent Achievements", rows, count);

    free(rows);
    cJSON_Delete(json);
}

void AchievementDetailView(const Achievement *achievement) {
    /* Always the colour badge here, even when locked — the grayscale "_lock"
       variant is only used to signal state in the list. */
    char badge_image[512];
    bool has_image = false;
    if (achievement->badge_name && achievement->badge_name[0]) {
        char badge_path[ICON_PATH_MAX];
        snprintf(badge_path, sizeof(badge_path), "/Badge/%s.png", achievement->badge_name);
        has_image = RA_GetImage(badge_path, badge_image, sizeof(badge_image));
    }

    char points[32];
    snprintf(points, sizeof(points), "%d (%d RetroPoints)",
        achievement->points, achievement->true_ratio);

    char unlocked_at[64];
    if (achievement->date_earned) {
        snprintf(unlocked_at, sizeof(unlocked_at), "%s (%s)",
            achievement->date_earned, achievement->hardcore ? "Hardcore" : "Softcore");
    } else {
        snprintf(unlocked_at, sizeof(unlocked_at), "Locked");
    }

    char type[48];
    FormatSnakeCase(achievement->type ? achievement->type : "-", type, sizeof(type));

    char won_by[64];
    snprintf(won_by, sizeof(won_by), "%d (%d hardcore)",
        achievement->num_awarded, achievement->num_awarded_hardcore);

    /* Built row by row, since not every source fills in every field. */
    ap_detail_info_pair info_pairs[6];
    int info_count = 0;

    info_pairs[info_count++] = (ap_detail_info_pair){ "Unlocked", unlocked_at };

    if (achievement->game_title) {
        info_pairs[info_count++] = (ap_detail_info_pair){ "Game", achievement->game_title };
    }

    info_pairs[info_count++] = (ap_detail_info_pair){ "Points", points };
    info_pairs[info_count++] = (ap_detail_info_pair){ "Type", type };
    info_pairs[info_count++] = (ap_detail_info_pair){
        "Author", achievement->author ? achievement->author : "-" };

    if (achievement->num_awarded >= 0) {
        info_pairs[info_count++] = (ap_detail_info_pair){ "Won By", won_by };
    }

    ap_detail_section sections[3];
    int section_count = 0;

    if (has_image) {
        /* The section stretches the texture to image_w x image_h with no aspect
           correction, and the default 300x200 turns a square badge into a wide
           rectangle. Badges are 64x64, so an equal, integer multiple keeps them
           square and avoids resampling artefacts. (ap_scale rather than AP_S:
           the macro reaches into a global only visible to the implementation
           unit.) */
        int badge_size = ap_scale(128);
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_IMAGE, .image_path = badge_image,
            .image_w = badge_size, .image_h = badge_size };
    }
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_DESCRIPTION, .title = "Description",
        .description = achievement->description ? achievement->description : "-" };
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_INFO, .title = "Details",
        .info_pairs = info_pairs,
        .info_count = info_count };

    /* The jump to the game is offered only when the achievement came from a
       cross-game list; otherwise that screen is already behind us. */
    ap_footer_item footer[2];
    int footer_count = 0;
    footer[footer_count++] = (ap_footer_item){ .button = AP_BTN_B, .label = "Back" };
    if (achievement->game_id > 0) {
        footer[footer_count++] = (ap_footer_item){ .button = AP_BTN_Y, .label = "Game" };
    }

    ap_detail_opts opts = {
        .title = achievement->title ? achievement->title : "-",
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = footer_count,
        .show_section_separator = true,
    };

    for (;;) {
        ap_detail_result result;
        ap_detail_screen(&opts, &result);

        /* A is unbound on detail screens: the widget still reports it, so
           reopen rather than treating it as a way out. */
        if (result.action == AP_DETAIL_ACTION) continue;

        if (result.action != AP_DETAIL_SECONDARY_ACTION || achievement->game_id <= 0) {
            break;
        }
        GameDetailView(achievement->game_id);
    }
}

void GameDetailView(int game_id) {
    if (!RequireSettings()) return;

    cJSON *json = RA_GetGameInfoAndUserProgress(Settings_Get()->username, game_id);

    cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "Title");
    if (!cJSON_IsString(title)) {
        cJSON_Delete(json);
        RequestFailedView("Could not load this game.");
        return;
    }

    /* Released is "2004-11-18", or "1992-06-02 00:00:00" on older records;
       either way keep just the date part. */
    char released[11];
    snprintf(released, sizeof(released), "%s", GetStringValue(json, "Released"));

    int n_achievements  = GetIntValue(json, "NumAchievements");
    int earned          = GetIntValue(json, "NumAwardedToUser");
    int earned_hardcore = GetIntValue(json, "NumAwardedToUserHardcore");

    /* Count, share and mode on one line. Where every unlock is hardcore — the
       common case — a bare suffix says so without spending a second row. */
    char progress[80];
    snprintf(progress, sizeof(progress), "%d / %d (%s)%s",
        earned, n_achievements, GetStringValue(json, "UserCompletion"),
        (earned > 0 && earned_hardcore == earned) ? " hardcore" : "");

    /* A separate row only when the two genuinely differ; all-hardcore is
       covered by the suffix above, and all-softcore needs no annotation. */
    bool mixed_modes = earned_hardcore > 0 && earned_hardcore < earned;
    char progress_hardcore[80];
    if (mixed_modes) {
        snprintf(progress_hardcore, sizeof(progress_hardcore), "%d / %d (%s)",
            earned_hardcore, n_achievements,
            GetStringValue(json, "UserCompletionHardcore"));
    }

    /* UserTotalPlaytime is seconds. */
    int playtime_min = GetIntValue(json, "UserTotalPlaytime") / 60;
    char playtime[32];
    snprintf(playtime, sizeof(playtime), "%dh %02dm", playtime_min / 60, playtime_min % 60);

    /* Values either point into json or into the buffers above, all of which
       outlive ap_detail_screen. */
    ap_detail_info_pair progress_pairs[4];
    int progress_count = 0;

    progress_pairs[progress_count++] = (ap_detail_info_pair){ "Achievements", progress };
    if (mixed_modes) {
        progress_pairs[progress_count++] = (ap_detail_info_pair){ "Hardcore", progress_hardcore };
    }
    progress_pairs[progress_count++] = (ap_detail_info_pair){ "Playtime", playtime };

    /* HighestAwardKind is absent until the game earns one, so skip the row
       rather than showing a dash. */
    const char *award = GetStringOrNull(json, "HighestAwardKind");
    char award_text[48];
    if (award) {
        FormatSnakeCase(award, award_text, sizeof(award_text));
        progress_pairs[progress_count++] = (ap_detail_info_pair){ "Award", award_text };
    }

    ap_detail_info_pair info_pairs[] = {
        { "Console",   GetStringValue(json, "ConsoleName") },
        { "Developer", GetStringValue(json, "Developer") },
        { "Publisher", GetStringValue(json, "Publisher") },
        { "Genre",     GetStringValue(json, "Genre") },
        { "Released",  released },
    };

    /* The detail screen loads image_path itself and owns the texture, so this
       buffer only has to outlive ap_detail_screen. */
    char title_image[512];
    bool has_image = RA_GetImage(
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "ImageTitle")),
        title_image, sizeof(title_image));

    ap_detail_section sections[3];
    int section_count = 0;

    if (has_image) {
        sections[section_count++] = (ap_detail_section){
            .type = AP_SECTION_IMAGE, .image_path = title_image };
    }
    /* Progress leads: it is what the screen is opened for. */
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_INFO, .title = "Your Progress",
        .info_pairs = progress_pairs,
        .info_count = progress_count };
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_INFO, .title = "Info",
        .info_pairs = info_pairs,
        .info_count = sizeof(info_pairs) / sizeof(info_pairs[0]) };

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_Y, .label = "Achievements" },
    };

    ap_detail_opts opts = {
        .title = cJSON_GetStringValue(title),
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .show_section_separator = true,
    };

    /* Y opens the achievements list and returns here; B leaves the screen.
       A is deliberately unbound — there is nothing here to select. */
    for (;;) {
        ap_detail_result result;
        ap_detail_screen(&opts, &result);

        /* The widget still reports A, so reopen rather than exiting on it. */
        if (result.action == AP_DETAIL_ACTION) continue;
        if (result.action != AP_DETAIL_SECONDARY_ACTION) break;

        AchievementsListView(cJSON_GetStringValue(title),
                             cJSON_GetObjectItemCaseSensitive(json, "Achievements"));
    }

    cJSON_Delete(json);
}

void RecentGamesView(void) {
    if (!RequireSettings()) return;

    cJSON *json = RA_GetUserRecentlyPlayedGames(Settings_Get()->username, RA_LIST_MAX);
    if (!json) {
        RequestFailedView("Could not load your games.");
        return;
    }

    /* An empty list is a valid answer, unlike a failed request. */
    int count = cJSON_GetArraySize(json);
    if (count <= 0) {
        cJSON_Delete(json);
        InfoView("No recently played games found for this user.");
        return;
    }

    GameListRow *rows = calloc((size_t)count, sizeof(*rows));
    if (!rows) {
        cJSON_Delete(json);
        return;
    }

    cJSON *game;
    int i = 0;
    cJSON_ArrayForEach(game, json) {
        rows[i++] = (GameListRow){
            .game_id   = GetIntValue(game, "GameID"),
            .title     = GetStringValue(game, "Title"),
            .icon_path = GetStringOrNull(game, "ImageIcon"),
        };
    }

    GameListView("Recently Played Games", rows, count);

    free(rows);
    cJSON_Delete(json);
}
