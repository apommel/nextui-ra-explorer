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

    MainView();

    ap_quit();
    return 0;
}
