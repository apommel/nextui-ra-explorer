#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

#define SETTINGS_VALUE_MAX 128

typedef struct {
    char username[SETTINGS_VALUE_MAX];
    char api_key[SETTINGS_VALUE_MAX];
} Settings;

/* Reads settings.json from user data into the in-memory settings. Returns
   false when the file is missing or unreadable, leaving the settings empty. */
bool Settings_Load(void);

/* Writes the in-memory settings back to settings.json. */
bool Settings_Save(void);

/* The current settings. Never NULL; fields are empty strings until loaded. */
const Settings *Settings_Get(void);

/* Replaces the in-memory settings. NULL is stored as an empty string. */
void Settings_Set(const char *username, const char *api_key);

/* True once both a username and an API key are present. */
bool Settings_IsConfigured(void);

#endif /* SETTINGS_H */
