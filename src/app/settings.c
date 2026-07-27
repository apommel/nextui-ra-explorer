#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "cjson/cJSON.h"

#include "util/paths.h"
#include "app/settings.h"

#define SETTINGS_FILENAME "settings.json"

static Settings g_settings;

static bool Settings_Path(char *out_path, size_t out_size) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), NULL)) {
        return false;
    }

    int written = snprintf(out_path, out_size, "%s/%s", dir, SETTINGS_FILENAME);
    return written > 0 && (size_t)written < out_size;
}

const Settings *Settings_Get(void) {
    return &g_settings;
}

void Settings_Set(const char *username, const char *api_key) {
    snprintf(g_settings.username, sizeof(g_settings.username), "%s", username ? username : "");
    snprintf(g_settings.api_key, sizeof(g_settings.api_key), "%s", api_key ? api_key : "");
}

bool Settings_IsConfigured(void) {
    return g_settings.username[0] != '\0' && g_settings.api_key[0] != '\0';
}

bool Settings_Load(void) {
    char path[512];
    if (!Settings_Path(path, sizeof(path))) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        return false; /* no settings yet — first run */
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(file);
        return false;
    }

    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';

    cJSON *json = cJSON_Parse(text);
    free(text);
    if (!json) {
        return false;
    }

    Settings_Set(
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "username")),
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "api_key")));
    cJSON_Delete(json);

    return Settings_IsConfigured();
}

bool Settings_Save(void) {
    char dir[400];
    if (!Paths_UserData(dir, sizeof(dir), NULL) || !Paths_MakeDirs(dir)) {
        return false;
    }

    char path[512];
    if (!Settings_Path(path, sizeof(path))) {
        return false;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return false;
    }
    cJSON_AddStringToObject(json, "username", g_settings.username);
    cJSON_AddStringToObject(json, "api_key", g_settings.api_key);

    char *text = cJSON_Print(json);
    cJSON_Delete(json);
    if (!text) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        free(text);
        return false;
    }

    bool ok = fputs(text, file) >= 0;
    ok = (fclose(file) == 0) && ok;
    free(text);

    /* The file holds an API key, so keep it owner-only */
    if (ok) chmod(path, 0600);

    return ok;
}
