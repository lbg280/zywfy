idf_component_register(
    SRCS "app_main.c" "voice_translate_app.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client esp_timer json
)
