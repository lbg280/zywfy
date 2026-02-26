#include "esp_http_client.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http_helper";

char *http_post_json(const char *url, const char *auth_header, const char *json_body)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "http client init failed");
        return NULL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (auth_header) {
        esp_http_client_set_header(client, "Authorization", auth_header);
    }

    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    if (esp_http_client_perform(client) != ESP_OK) {
        ESP_LOGE(TAG, "http request failed");
        esp_http_client_cleanup(client);
        return NULL;
    }

    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "http status=%d", status);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int len = esp_http_client_get_content_length(client);
    if (len <= 0) {
        len = 4096;
    }

    char *buf = calloc(1, len + 1);
    if (!buf) {
        esp_http_client_cleanup(client);
        return NULL;
    }

    int read_len = esp_http_client_read_response(client, buf, len);
    if (read_len < 0) {
        free(buf);
        buf = NULL;
    } else {
        buf[read_len] = '\0';
    }

    esp_http_client_cleanup(client);
    return buf;
}
