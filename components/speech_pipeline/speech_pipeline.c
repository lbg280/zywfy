#include "speech_pipeline.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *http_post_json(const char *url, const char *auth_header, const char *json_body);

static const char *TAG = "speech_pipeline";
static speech_pipeline_config_t g_cfg = {0};

// ------- 与小智原有驱动对接：仅保留接口，不破坏原始实现 -------
// 在移植时将这些 extern 指向小智源码中的麦克风/音频模块。
extern size_t xiaozhi_mic_read(int16_t *dst, size_t max_samples);
extern int xiaozhi_vad_is_speech(const int16_t *pcm, size_t samples);

// 百度语音要求 audio 为 base64；为演示保留简化版本。
static char *encode_pcm_to_base64(const int16_t *pcm, size_t samples)
{
    // TODO: 替换为真实 base64 实现
    (void)pcm;
    (void)samples;
    return strdup("BASE64_PCM_PLACEHOLDER");
}

static char *baidu_asr_recognize(const int16_t *pcm, size_t samples)
{
    char *audio_b64 = encode_pcm_to_base64(pcm, samples);
    if (!audio_b64) {
        return NULL;
    }

    char body[2048];
    snprintf(body, sizeof(body),
             "{\"format\":\"pcm\",\"rate\":%d,\"channel\":1,\"cuid\":\"esp32c3\","
             "\"token\":\"%s\",\"len\":%u,\"speech\":\"%s\"}",
             g_cfg.sample_rate, g_cfg.baidu_secret_key, (unsigned)(samples * sizeof(int16_t)), audio_b64);

    free(audio_b64);

    char *resp = http_post_json("https://vop.baidu.com/server_api", NULL, body);
    if (!resp) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        return NULL;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(result) || cJSON_GetArraySize(result) == 0) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *first = cJSON_GetArrayItem(result, 0);
    char *text = strdup(cJSON_GetStringValue(first));
    cJSON_Delete(root);
    return text;
}

static char *deepseek_translate_to_zh(const char *src_text)
{
    char body[2048];
    snprintf(body, sizeof(body),
             "{\"model\":\"deepseek-chat\",\"messages\":[{\"role\":\"system\",\"content\":\"你是翻译助手，请将输入内容翻译为简体中文，不要解释。\"},"
             "{\"role\":\"user\",\"content\":\"%s\"}]}",
             src_text);

    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", g_cfg.deepseek_api_key);

    char *resp = http_post_json("https://api.deepseek.com/chat/completions", auth, body);
    if (!resp) {
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) {
        return NULL;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    cJSON *choice0 = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *msg = choice0 ? cJSON_GetObjectItem(choice0, "message") : NULL;
    cJSON *content = msg ? cJSON_GetObjectItem(msg, "content") : NULL;

    char *translated = NULL;
    if (cJSON_IsString(content)) {
        translated = strdup(content->valuestring);
    }

    cJSON_Delete(root);
    return translated;
}

void speech_pipeline_init(const speech_pipeline_config_t *cfg)
{
    g_cfg = *cfg;
    ESP_LOGI(TAG, "pipeline init: sample_rate=%d, chunk_ms=%d", g_cfg.sample_rate, g_cfg.chunk_ms);
}

void speech_pipeline_feed_pcm(const int16_t *pcm, size_t samples)
{
    if (!xiaozhi_vad_is_speech(pcm, samples)) {
        return;
    }

    char *asr_text = baidu_asr_recognize(pcm, samples);
    if (!asr_text) {
        ESP_LOGW(TAG, "ASR failed");
        return;
    }

    char *zh_text = deepseek_translate_to_zh(asr_text);
    if (!zh_text) {
        ESP_LOGW(TAG, "translate failed, fallback ASR text");
        printf("ASR: %s\n", asr_text);
        free(asr_text);
        return;
    }

    printf("识别: %s\n", asr_text);
    printf("翻译: %s\n", zh_text);

    free(asr_text);
    free(zh_text);
}

void speech_pipeline_process(void)
{
    const size_t samples_per_chunk = (g_cfg.sample_rate * g_cfg.chunk_ms) / 1000;
    int16_t *buf = calloc(samples_per_chunk, sizeof(int16_t));
    if (!buf) {
        ESP_LOGE(TAG, "alloc audio buffer failed");
        return;
    }

    while (1) {
        size_t got = xiaozhi_mic_read(buf, samples_per_chunk);
        if (got > 0) {
            speech_pipeline_feed_pcm(buf, got);
        }
    }
}
