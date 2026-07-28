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

/* "win_condition" -> "Win Condition".

   RA's achievement Type is snake_case, and the set of values is open-ended, so
   this transforms generically instead of mapping the ones we know about — an
   unrecognised value still renders sensibly rather than falling back to raw. */
static void FormatSnakeCase(const char *value, char *out, size_t out_size) {
    size_t i = 0;
    bool word_start = true;

    for (const char *p = value; *p && i + 1 < out_size; p++) {
        if (*p == '_') {
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

/* ── Views ───────────────────────────────────────────────────────────────── */

void MainView(void) {
    enum { MENU_RECENT_GAMES, MENU_RECENT_ACHIEVEMENTS };

    ap_list_item items[] = {
        [MENU_RECENT_GAMES]        = { .label = "Recently Played Games" },
        [MENU_RECENT_ACHIEVEMENTS] = { .label = "Recent Achievements" },
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
            if (result.selected_index == MENU_RECENT_GAMES) {
                RecentGamesView();
            } else if (result.selected_index == MENU_RECENT_ACHIEVEMENTS) {
                RecentAchievementsView();
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
    ap_list_item *items = calloc((size_t)count, sizeof(*items));
    IconSlot *slots = calloc((size_t)count, sizeof(*slots));
    if (!entries || !items || !slots) {
        free(entries);
        free(items);
        free(slots);
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
        cJSON *entry = entries[i].achievement;

        /* RA publishes a grayscale variant of every badge at "<name>_lock.png",
           so locked rows need no local image processing. */
        const char *badge = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(entry, "BadgeName"));
        if (badge && badge[0]) {
            snprintf(slots[i].path, sizeof(slots[i].path), "/Badge/%s%s.png",
                     badge, entries[i].unlocked ? "" : "_lock");
        }

        char points[32];
        snprintf(points, sizeof(points), "%d pts", GetIntValue(entry, "Points"));

        /* label and metadata point into the caller's JSON and must not be
           freed; only trailing_text and the texture are owned here. */
        items[i] = (ap_list_item){
            .label         = GetStringValue(entry, "Title"),
            .metadata      = GetStringValue(entry, "Description"),
            .trailing_text = strdup(points),
        };
    }

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
    };

    IconLoader loader = { .slots = slots, .count = count };

    ap_list_opts opts = ap_list_default_opts(game_title, items, count);
    opts.footer       = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);
    IconLoader_Attach(&opts, &loader);

    /* Stay on the list until the user backs out with B; A opens one
       achievement and returns here. */
    for (;;) {
        ap_list_result result;
        if (ap_list(&opts, &result) != AP_OK) break;
        if (result.selected_index < 0 || result.selected_index >= count) break;

        opts.initial_index       = result.selected_index;
        opts.visible_start_index = result.visible_start_index;

        /* entries and items stay index-aligned: reordering is not enabled. */
        Achievement selected =
            AchievementFromGameProgress(entries[result.selected_index].achievement);
        AchievementDetailView(&selected);
    }

    for (i = 0; i < count; i++) {
        free((char *)items[i].trailing_text);
    }
    IconLoader_DestroyTextures(items, count);
    free(items);
    free(entries);
    free(slots);
}

/* How far back "recent" reaches. The endpoint's own default is 60 minutes,
   which is too narrow to be useful as a browsable list. */
#define RECENT_ACHIEVEMENTS_MINUTES (30 * 24 * 60)

void RecentAchievementsView(void) {
    if (!Settings_IsConfigured()) {
        InfoView("Set your username and API key in Settings first.");
        return;
    }

    cJSON *json = RA_GetUserRecentAchievements(Settings_Get()->username,
                                               RECENT_ACHIEVEMENTS_MINUTES);
    if (!json) {
        InfoView("Failed to get recent achievements from RetroAchievements.");
        return;
    }

    /* An empty array is a valid answer here, unlike a failed request. */
    int count = cJSON_GetArraySize(json);
    if (count <= 0) {
        cJSON_Delete(json);
        InfoView("No achievements unlocked in the past week.");
        return;
    }

    cJSON **sources = calloc((size_t)count, sizeof(*sources));
    ap_list_item *items = calloc((size_t)count, sizeof(*items));
    IconSlot *slots = calloc((size_t)count, sizeof(*slots));
    if (!sources || !items || !slots) {
        free(sources);
        free(items);
        free(slots);
        cJSON_Delete(json);
        return;
    }

    /* The API already returns most recent first, so the response order stands
       — there is no cross-game DisplayOrder to sort on. */
    cJSON *entry;
    int i = 0;
    cJSON_ArrayForEach(entry, json) {
        sources[i] = entry;

        /* Recent achievements are unlocked by definition, so always the colour
           badge. BadgeURL is already a full path; BadgeName is the fallback. */
        const char *badge_url = GetStringOrNull(entry, "BadgeURL");
        const char *badge_name = GetStringOrNull(entry, "BadgeName");
        if (badge_url) {
            snprintf(slots[i].path, sizeof(slots[i].path), "%s", badge_url);
        } else if (badge_name) {
            snprintf(slots[i].path, sizeof(slots[i].path), "/Badge/%s.png", badge_name);
        }

        /* Labels point into json, which outlives the list. */
        items[i] = (ap_list_item){
            .label         = GetStringValue(entry, "Title"),
            .trailing_text = GetStringValue(entry, "GameTitle"),
        };
        i++;
    }

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
    };

    IconLoader loader = { .slots = slots, .count = count };

    ap_list_opts opts = ap_list_default_opts("Recent Achievements", items, count);
    opts.footer       = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);
    IconLoader_Attach(&opts, &loader);

    for (;;) {
        ap_list_result result;
        if (ap_list(&opts, &result) != AP_OK) break;
        if (result.selected_index < 0 || result.selected_index >= count) break;

        opts.initial_index       = result.selected_index;
        opts.visible_start_index = result.visible_start_index;

        Achievement selected = AchievementFromRecent(sources[result.selected_index]);
        AchievementDetailView(&selected);
    }

    IconLoader_DestroyTextures(items, count);
    free(sources);
    free(items);
    free(slots);
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

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
    };

    ap_detail_opts opts = {
        .title = achievement->title ? achievement->title : "-",
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .show_section_separator = true,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);
}

void GameDetailView(int game_id) {
    cJSON *json = RA_GetGameInfoAndUserProgress(Settings_Get()->username, game_id);

    cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "Title");
    if (!cJSON_IsString(title)) {
        cJSON_Delete(json);
        InfoView("Failed to get game details from RetroAchievements.");
        return;
    }

    /* Released arrives as "1992-06-02 00:00:00"; keep just the date part. */
    char released[11];
    snprintf(released, sizeof(released), "%s", GetStringValue(json, "Released"));

    int n_achievements = GetIntValue(json, "NumAchievements");
    int playtime_min = GetIntValue(json, "UserTotalPlaytime") / 60;

    char earned[32];
    snprintf(earned, sizeof(earned), "%d / %d",
        GetIntValue(json, "NumAwardedToUser"), n_achievements);

    char earned_hardcore[32];
    snprintf(earned_hardcore, sizeof(earned_hardcore), "%d / %d",
        GetIntValue(json, "NumAwardedToUserHardcore"), n_achievements);

    char playtime[32];
    snprintf(playtime, sizeof(playtime), "%dh %02dm", playtime_min / 60, playtime_min % 60);

    /* Values either point into json or into the buffers above, all of which
       outlive ap_detail_screen. */
    ap_detail_info_pair info_pairs[] = {
        { "Console",   GetStringValue(json, "ConsoleName") },
        { "Developer", GetStringValue(json, "Developer") },
        { "Publisher", GetStringValue(json, "Publisher") },
        { "Genre",     GetStringValue(json, "Genre") },
        { "Released",  released },
    };

    ap_detail_info_pair progress_pairs[] = {
        { "Achievements", earned },
        { "Hardcore",     earned_hardcore },
        { "Completion",   GetStringValue(json, "UserCompletion") },
        { "Playtime",     playtime },
        { "Award",        GetStringValue(json, "HighestAwardKind") },
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
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_INFO, .title = "Info",
        .info_pairs = info_pairs,
        .info_count = sizeof(info_pairs) / sizeof(info_pairs[0]) };
    sections[section_count++] = (ap_detail_section){
        .type = AP_SECTION_INFO, .title = "Your Progress",
        .info_pairs = progress_pairs,
        .info_count = sizeof(progress_pairs) / sizeof(progress_pairs[0]) };

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_A, .label = "Achievements", .is_confirm = true },
    };

    ap_detail_opts opts = {
        .title = cJSON_GetStringValue(title),
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .show_section_separator = true,
    };

    /* A opens the achievements list and returns here; B leaves the screen. */
    for (;;) {
        ap_detail_result result;
        ap_detail_screen(&opts, &result);
        if (result.action != AP_DETAIL_ACTION) break;

        AchievementsListView(cJSON_GetStringValue(title),
                             cJSON_GetObjectItemCaseSensitive(json, "Achievements"));
    }

    cJSON_Delete(json);
}

void RecentGamesView(void) {
    if (!Settings_IsConfigured()) {
        InfoView("Set your username and API key in Settings first.");
        return;
    }

    cJSON *json = RA_GetUserRecentlyPlayedGames(Settings_Get()->username, 50);

    int n_games = cJSON_GetArraySize(json);
    if (n_games <= 0) {
        cJSON_Delete(json);
        InfoView("Failed to get list of games from RetroAchievements.");
        return;
    }

    ap_list_item items[n_games];
    IconSlot slots[n_games];
    memset(slots, 0, sizeof(slots));

    cJSON *game;
    int i = 0;
    cJSON_ArrayForEach(game, json) {
        cJSON *title = cJSON_GetObjectItemCaseSensitive(game, "Title");
        cJSON *console = cJSON_GetObjectItemCaseSensitive(game, "ConsoleName");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(game, "GameID");
        
        char item_label[128];
        snprintf(item_label, 128, "%s (%s)", 
            cJSON_GetStringValue(title),
            cJSON_GetStringValue(console)
        );

        char id_metadata[RA_GAME_ID_LENGTH];
        snprintf(id_metadata, RA_GAME_ID_LENGTH, "%d", (int)cJSON_GetNumberValue(id));

        /* The icon is fetched lazily by IconLoader once the list is up. */
        const char *icon = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(game, "ImageIcon"));
        if (icon && icon[0]) {
            snprintf(slots[i].path, sizeof(slots[i].path), "%s", icon);
        }

        /* These buffers die with this iteration, so hand the list copies. */
        items[i] = (ap_list_item){
            .label = strdup(item_label),
            .metadata = strdup(id_metadata),
        };
        i++;
    }

    IconLoader loader = { .slots = slots, .count = n_games };

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Back" },
        { .button = AP_BTN_A, .label = "Details", .is_confirm = true },
    };

    ap_list_opts opts = ap_list_default_opts("Recently Played Games", items, n_games);
    opts.footer       = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);
    IconLoader_Attach(&opts, &loader);

    /* Stay on the list until the user backs out with B */
    for (;;) {
        ap_list_result result;
        if (ap_list(&opts, &result) != AP_OK || result.selected_index < 0) break;

        /* Come back with the cursor and scroll position the user left. */
        opts.initial_index       = result.selected_index;
        opts.visible_start_index = result.visible_start_index;

        /* Read from result.items, since the list may have reordered them. */
        GameDetailView(atoi(result.items[result.selected_index].metadata));
    }

    for (int j = 0; j < n_games; j++) {
        free((char *)items[j].label);
        free((char *)items[j].metadata);
        if (items[j].image) SDL_DestroyTexture(items[j].image);
    }
    cJSON_Delete(json);
}
