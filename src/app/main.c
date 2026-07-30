#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include "app/settings.h"
#include "app/views.h"

int main(void) {
    ap_config cfg = {
        .window_title = "RA Explorer",
        .font_path    = AP_PLATFORM_IS_DEVICE ? NULL : "font.ttf",
        .is_nextui    = AP_PLATFORM_IS_DEVICE,
    };
    ap_init(&cfg);

    Settings_Load();
    if (!Settings_IsConfigured()) {
        InfoView("Enter your RetroAchievements username and API key, then press "
                 "START to save. Your API key is on the RetroAchievements "
                 "website, under Settings > Applications.");
        SettingsView();
    }

    MainView();

    ap_quit();
    return 0;
}
