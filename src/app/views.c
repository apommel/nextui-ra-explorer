#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "app/settings.h"
#include "app/views.h"
#include "ra_api/images.h"
#include "ra_api/ra_api.h"


void MainView() {
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

    ap_options_item items[] = {
        { .label = "Username", .type = AP_OPT_KEYBOARD,
          .options = username_option, .option_count = 1, .selected_option = 0 },
        { .label = "API Key", .type = AP_OPT_KEYBOARD,
          .options = api_key_option, .option_count = 1, .selected_option = 0 },
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

    ap_options_list_result result;
    ap_options_list(&opts, &result);

    if (result.action == AP_ACTION_CONFIRMED) {
        Settings_Set(username_option[0].value, api_key_option[0].value);
        if (!Settings_Save()) {
            ErrorView("Failed to save settings.");
        }
    }

    free((void *)username_option[0].label);
    free((void *)username_option[0].value);
    free((void *)api_key_option[0].label);
    free((void *)api_key_option[0].value);
}

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
    };

    ap_detail_opts opts = {
        .title = cJSON_GetStringValue(title),
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = sizeof(footer) / sizeof(footer[0]),
        .show_section_separator = true,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

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

        /* ap_list owns neither the strings nor the texture, so both are freed
           after it returns. A missing icon just leaves .image NULL. */
        char icon_path[512];
        SDL_Texture *icon = NULL;
        if (RA_GetImage(
                cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(game, "ImageIcon")),
                icon_path, sizeof(icon_path))) {
            icon = ap_load_image(icon_path);
        }

        /* These buffers die with this iteration, so hand the list copies. */
        items[i] = (ap_list_item){
            .label = strdup(item_label),
            .metadata = strdup(id_metadata),
            .image = icon,
        };
        i++;
    }

    ap_list_opts opts = ap_list_default_opts("Recently Played Games", items, n_games);
    opts.show_images = true;

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
