#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *baidu_api_key;
    const char *baidu_secret_key;
    const char *deepseek_api_key;
    int sample_rate;
    int chunk_ms;
    int serial_baud;
} speech_pipeline_config_t;

/**
 * @brief 初始化语音识别+翻译流水线
 */
void speech_pipeline_init(const speech_pipeline_config_t *cfg);

/**
 * @brief 推送一段 PCM 数据（16bit/mono）给流水线
 */
void speech_pipeline_feed_pcm(const int16_t *pcm, size_t samples);

/**
 * @brief 主循环处理，建议在独立任务中调用
 */
void speech_pipeline_process(void);

#ifdef __cplusplus
}
#endif
