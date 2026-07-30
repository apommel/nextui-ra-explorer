#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

#define SETTINGS_VALUE_MAX 128

typedef struct {
    char username[SETTINGS_VALUE_MAX];
    char api_key[SETTINGS_VALUE_MAX];
    /* Stable id for the account. Usernames can be changed on the website, so
       requests prefer this once it is known. Empty until resolved, which is
       never fatal — see Settings_UserRef. */
    char ulid[SETTINGS_VALUE_MAX];
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

/* How to name the configured user in a request: the ULID when it has been
   resolved, otherwise the username. Both are accepted by every user-scoped
   endpoint, so an unresolved ULID only costs stability across a rename. */
const char *Settings_UserRef(void);

#endif /* SETTINGS_H */
