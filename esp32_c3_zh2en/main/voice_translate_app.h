#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*voice_translate_result_cb_t)(const char *zh_text, const char *en_text);

typedef struct {
    const char *asr_url;          // 例如: https://api.example.com/v1/asr
    const char *translate_url;    // 例如: https://api.example.com/v1/translate
    const char *api_key;          // 放到 HTTP Header: Authorization: Bearer <api_key>

    int sample_rate;              // 推荐 16000
    int vad_energy_threshold;     // 推荐 600~1200 之间按麦克风灵敏度调参
    int vad_silence_ms;           // 连续静音多久视为一句结束，推荐 700~1000ms
    int max_utterance_ms;         // 最长一句语音，推荐 10~15 秒

    voice_translate_result_cb_t on_result;
} voice_translate_config_t;

bool voice_translate_init(const voice_translate_config_t *cfg);
void voice_translate_feed_pcm(const int16_t *pcm, size_t samples);

#ifdef __cplusplus
}
#endif
