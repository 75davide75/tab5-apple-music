idf_component_register(SRCS "ui.c" "icons.c"
                       INCLUDE_DIRS "."
                       REQUIRES core lvgl__lvgl esp_timer
                       EMBED_FILES "fonts/Inter-Regular.ttf" "fonts/Inter-SemiBold.ttf")
