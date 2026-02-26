#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "voice_translate_app.h"

static const char *TAG = "app_main";

static void on_translate_result(const char *zh, const char *en) {
    // 这里可改成调用你现有项目里的 TTS 播放接口
    ESP_LOGI(TAG, "翻译结果: [%s] -> [%s]", zh, en);
}

void app_main(void) {
    voice_translate_config_t cfg = {
        .asr_url = "https://your-api.example.com/v1/asr",
        .translate_url = "https://your-api.example.com/v1/translate",
        .api_key = "YOUR_API_KEY",
        .sample_rate = 16000,
        .vad_energy_threshold = 800,
        .vad_silence_ms = 800,
        .max_utterance_ms = 12000,
        .on_result = on_translate_result,
    };

    if (!voice_translate_init(&cfg)) {
        ESP_LOGE(TAG, "voice_translate_init failed");
        return;
    }

    ESP_LOGI(TAG, "voice translate ready");

    // 示例：实际中你应在 I2S 麦克风回调里调用 voice_translate_feed_pcm()
    // 这里仅保活任务。
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
