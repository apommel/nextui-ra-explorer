#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

#define SETTINGS_VALUE_MAX 128

typedef struct {
    char username[SETTINGS_VALUE_MAX];
    char api_key[SETTINGS_VALUE_MAX];
    bool unlocked_first; /* List unlocked achievements before locked ones. */
} Settings;

/* Reads settings.json from user data into the in-memory settings. Returns
   false when the file is missing or unreadable, leaving the settings empty. */
bool Settings_Load(void);

/* Writes the in-memory settings back to settings.json. */
bool Settings_Save(void);

/* The current settings. Never NULL; fields are empty strings until loaded. */
const Settings *Settings_Get(void);

/* Replaces the in-memory settings. Copy from Settings_Get(), edit, pass back. */
void Settings_Set(const Settings *settings);

/* True once both a username and an API key are present. */
bool Settings_IsConfigured(void);

#endif /* SETTINGS_H */
