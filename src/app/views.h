#ifndef VIEWS_H
#define VIEWS_H

/* Blocking full-screen views. Each returns once the user backs out of it. */

/* Main application view. */
void MainView(void);

/* Modal message with a single OK button. */
void ErrorView(const char *message);

/* Username and API key editor; writes to settings.json on save. */
void SettingsView(void);

/* The signed-in user's recently played games; opens GameDetailView on select. */
void RecentGamesView(void);

/* Game info and the signed-in user's progress for one game. */
void GameDetailView(int game_id);

#endif /* VIEWS_H */
