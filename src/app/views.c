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

/* Returns the numeric field, or 0 when it is missing or not a number.
   cJSON_GetNumberValue yields NaN for those, and casting NaN to int is UB. */
static int GetIntValue(cJSON *json, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsNumber(item) ? (int)cJSON_GetNumberValue(item) : 0;
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
    ap_list_item items[] = {
        { .label = "Recently Played Games" },
    };

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Quit" },
        { .button = AP_BTN_X, .label = "Settings" },
        { .button = AP_BTN_A, .label = "Select", .is_confirm = true },
    };

    ap_list_opts opts = ap_list_default_opts("RA Explorer", items, 1);
    opts.footer       = footer;
    opts.footer_count = 3;
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
        } else if (result.action == AP_ACTION_SELECTED && result.selected_index == 0) {
            RecentGamesView();
        }
    }
}

void ErrorView(const char *message) {
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
                ErrorView("Some cached images could not be deleted.");
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
                ErrorView("Failed to save settings.");
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
        ErrorView("This game has no achievements.");
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
    };

    IconLoader loader = { .slots = slots, .count = count };

    ap_list_opts opts = ap_list_default_opts(game_title, items, count);
    opts.footer       = footer;
    opts.footer_count = sizeof(footer) / sizeof(footer[0]);
    IconLoader_Attach(&opts, &loader);

    ap_list_result result;
    ap_list(&opts, &result);

    for (i = 0; i < count; i++) {
        free((char *)items[i].trailing_text);
    }
    IconLoader_DestroyTextures(items, count);
    free(items);
    free(entries);
    free(slots);
}

void GameDetailView(int game_id) {
    cJSON *json = RA_GetGameInfoAndUserProgress(Settings_Get()->username, game_id);

    cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "Title");
    if (!cJSON_IsString(title)) {
        cJSON_Delete(json);
        ErrorView("Failed to get game details from RetroAchievements.");
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
        ErrorView("Set your username and API key in Settings first.");
        return;
    }

    cJSON *json = RA_GetUserRecentlyPlayedGames(Settings_Get()->username);

    int n_games = cJSON_GetArraySize(json);
    if (n_games <= 0) {
        cJSON_Delete(json);
        ErrorView("Failed to get list of games from RetroAchievements.");
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

    ap_list_opts opts = ap_list_default_opts("Recently Played Games", items, n_games);
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
