idf_component_register(SRCS "speech_pipeline.c" "http_client_helper.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_client json)
