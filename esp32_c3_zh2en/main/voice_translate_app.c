#include "voice_translate_app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "voice_translate"

#define WAV_HEADER_SIZE 44
#define HTTP_RECV_BUF   4096

typedef struct {
    voice_translate_config_t cfg;

    int16_t *pcm_buf;
    size_t pcm_capacity_samples;
    size_t pcm_len_samples;

    int64_t last_voice_ms;
    bool in_speech;
} voice_translate_ctx_t;

static voice_translate_ctx_t g_ctx = {0};

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static int calc_frame_energy(const int16_t *pcm, size_t samples) {
    if (!pcm || samples == 0) return 0;

    uint64_t sum = 0;
    for (size_t i = 0; i < samples; ++i) {
        sum += llabs((long long)pcm[i]);
    }
    return (int)(sum / samples);
}

static uint8_t *build_wav_mono_16k(const int16_t *pcm, size_t samples, int sample_rate, size_t *out_len) {
    if (!pcm || samples == 0 || !out_len) return NULL;

    size_t data_bytes = samples * sizeof(int16_t);
    size_t total = WAV_HEADER_SIZE + data_bytes;

    uint8_t *wav = (uint8_t *)malloc(total);
    if (!wav) return NULL;
    memset(wav, 0, total);

    // RIFF header
    memcpy(wav + 0, "RIFF", 4);
    uint32_t chunk_size = (uint32_t)(36 + data_bytes);
    memcpy(wav + 4, &chunk_size, 4);
    memcpy(wav + 8, "WAVE", 4);

    // fmt chunk
    memcpy(wav + 12, "fmt ", 4);
    uint32_t subchunk1_size = 16;
    uint16_t audio_format = 1; // PCM
    uint16_t channels = 1;
    uint32_t sr = (uint32_t)sample_rate;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sr * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;

    memcpy(wav + 16, &subchunk1_size, 4);
    memcpy(wav + 20, &audio_format, 2);
    memcpy(wav + 22, &channels, 2);
    memcpy(wav + 24, &sr, 4);
    memcpy(wav + 28, &byte_rate, 4);
    memcpy(wav + 32, &block_align, 2);
    memcpy(wav + 34, &bits_per_sample, 2);

    // data chunk
    memcpy(wav + 36, "data", 4);
    uint32_t data_size_u32 = (uint32_t)data_bytes;
    memcpy(wav + 40, &data_size_u32, 4);
    memcpy(wav + WAV_HEADER_SIZE, pcm, data_bytes);

    *out_len = total;
    return wav;
}

static char *http_post_json(const char *url, const char *api_key, const char *payload) {
    if (!url || !payload) return NULL;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return NULL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (api_key && api_key[0]) {
        char auth[256] = {0};
        snprintf(auth, sizeof(auth), "Bearer %s", api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }

    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }

    int status = esp_http_client_get_status_code(client);
    int len = esp_http_client_get_content_length(client);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP status=%d", status);
        esp_http_client_cleanup(client);
        return NULL;
    }

    if (len <= 0 || len > HTTP_RECV_BUF) {
        len = HTTP_RECV_BUF - 1;
    }

    char *resp = (char *)calloc(1, len + 1);
    if (!resp) {
        esp_http_client_cleanup(client);
        return NULL;
    }

    int readn = esp_http_client_read_response(client, resp, len);
    if (readn < 0) {
        free(resp);
        esp_http_client_cleanup(client);
        return NULL;
    }
    resp[readn] = '\0';

    esp_http_client_cleanup(client);
    return resp;
}

static char *extract_json_text_field(const char *json, const char *field) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return NULL;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, field);
    if (!cJSON_IsString(item) || !item->valuestring) {
        cJSON_Delete(root);
        return NULL;
    }

    char *out = strdup(item->valuestring);
    cJSON_Delete(root);
    return out;
}

static char *call_asr(const uint8_t *wav, size_t wav_len) {
    // 为减少示例复杂度，这里假定服务端支持 base64-wav JSON：
    // {"audio_base64":"...","format":"wav","lang":"zh"}
    // 返回：{"text":"你好世界"}

    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_len = ((wav_len + 2) / 3) * 4;
    char *b64 = (char *)malloc(b64_len + 1);
    if (!b64) return NULL;

    size_t i = 0, j = 0;
    while (i + 2 < wav_len) {
        uint32_t n = (wav[i] << 16) | (wav[i + 1] << 8) | wav[i + 2];
        b64[j++] = b64_table[(n >> 18) & 63];
        b64[j++] = b64_table[(n >> 12) & 63];
        b64[j++] = b64_table[(n >> 6) & 63];
        b64[j++] = b64_table[n & 63];
        i += 3;
    }
    if (i < wav_len) {
        uint32_t n = wav[i] << 16;
        b64[j++] = b64_table[(n >> 18) & 63];
        if (i + 1 < wav_len) {
            n |= wav[i + 1] << 8;
            b64[j++] = b64_table[(n >> 12) & 63];
            b64[j++] = b64_table[(n >> 6) & 63];
            b64[j++] = '=';
        } else {
            b64[j++] = b64_table[(n >> 12) & 63];
            b64[j++] = '=';
            b64[j++] = '=';
        }
    }
    b64[j] = '\0';

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "audio_base64", b64);
    cJSON_AddStringToObject(root, "format", "wav");
    cJSON_AddStringToObject(root, "lang", "zh");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(b64);

    if (!payload) return NULL;

    char *resp = http_post_json(g_ctx.cfg.asr_url, g_ctx.cfg.api_key, payload);
    free(payload);
    if (!resp) return NULL;

    char *zh_text = extract_json_text_field(resp, "text");
    free(resp);
    return zh_text;
}

static char *call_translate(const char *zh_text) {
    // 假定翻译服务入参：{"source":"zh","target":"en","text":"你好"}
    // 出参：{"translation":"Hello"}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "source", "zh");
    cJSON_AddStringToObject(root, "target", "en");
    cJSON_AddStringToObject(root, "text", zh_text);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) return NULL;

    char *resp = http_post_json(g_ctx.cfg.translate_url, g_ctx.cfg.api_key, payload);
    free(payload);
    if (!resp) return NULL;

    char *en_text = extract_json_text_field(resp, "translation");
    free(resp);
    return en_text;
}

static void process_utterance(void) {
    if (g_ctx.pcm_len_samples == 0) {
        return;
    }

    size_t wav_len = 0;
    uint8_t *wav = build_wav_mono_16k(g_ctx.pcm_buf, g_ctx.pcm_len_samples, g_ctx.cfg.sample_rate, &wav_len);
    if (!wav) {
        ESP_LOGE(TAG, "build_wav failed");
        g_ctx.pcm_len_samples = 0;
        return;
    }

    char *zh = call_asr(wav, wav_len);
    free(wav);

    if (!zh || zh[0] == '\0') {
        ESP_LOGW(TAG, "ASR empty");
        free(zh);
        g_ctx.pcm_len_samples = 0;
        return;
    }

    char *en = call_translate(zh);
    if (!en || en[0] == '\0') {
        ESP_LOGW(TAG, "translate empty, zh=%s", zh);
        free(zh);
        free(en);
        g_ctx.pcm_len_samples = 0;
        return;
    }

    ESP_LOGI(TAG, "ZH: %s", zh);
    ESP_LOGI(TAG, "EN: %s", en);

    if (g_ctx.cfg.on_result) {
        g_ctx.cfg.on_result(zh, en);
    }

    free(zh);
    free(en);
    g_ctx.pcm_len_samples = 0;
}

bool voice_translate_init(const voice_translate_config_t *cfg) {
    if (!cfg || !cfg->asr_url || !cfg->translate_url) {
        return false;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *cfg;

    if (g_ctx.cfg.sample_rate <= 0) g_ctx.cfg.sample_rate = 16000;
    if (g_ctx.cfg.vad_energy_threshold <= 0) g_ctx.cfg.vad_energy_threshold = 800;
    if (g_ctx.cfg.vad_silence_ms <= 0) g_ctx.cfg.vad_silence_ms = 800;
    if (g_ctx.cfg.max_utterance_ms <= 0) g_ctx.cfg.max_utterance_ms = 12000;

    g_ctx.pcm_capacity_samples = (size_t)((int64_t)g_ctx.cfg.sample_rate * g_ctx.cfg.max_utterance_ms / 1000);
    g_ctx.pcm_buf = (int16_t *)calloc(g_ctx.pcm_capacity_samples, sizeof(int16_t));
    if (!g_ctx.pcm_buf) {
        return false;
    }

    g_ctx.last_voice_ms = now_ms();
    return true;
}

void voice_translate_feed_pcm(const int16_t *pcm, size_t samples) {
    if (!pcm || samples == 0 || !g_ctx.pcm_buf) {
        return;
    }

    int e = calc_frame_energy(pcm, samples);
    bool voiced = e >= g_ctx.cfg.vad_energy_threshold;
    int64_t t = now_ms();

    if (voiced) {
        g_ctx.in_speech = true;
        g_ctx.last_voice_ms = t;
    }

    if (g_ctx.in_speech) {
        size_t remain = g_ctx.pcm_capacity_samples - g_ctx.pcm_len_samples;
        size_t to_copy = samples < remain ? samples : remain;
        if (to_copy > 0) {
            memcpy(&g_ctx.pcm_buf[g_ctx.pcm_len_samples], pcm, to_copy * sizeof(int16_t));
            g_ctx.pcm_len_samples += to_copy;
        }

        bool timeout_silence = (t - g_ctx.last_voice_ms) >= g_ctx.cfg.vad_silence_ms;
        bool overflow = g_ctx.pcm_len_samples >= g_ctx.pcm_capacity_samples;
        if (timeout_silence || overflow) {
            g_ctx.in_speech = false;
            process_utterance();
        }
    }
}
