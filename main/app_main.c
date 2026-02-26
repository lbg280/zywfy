#include "speech_pipeline.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 兼容性说明：下列接口由小智源码中的硬件驱动提供。
// 这里仅声明，不改动原驱动，实现“可插拔”接入。
extern void xiaozhi_board_init(void);

static void pipeline_task(void *arg)
{
    (void)arg;

    speech_pipeline_config_t cfg = {
        .baidu_api_key = "YOUR_BAIDU_API_KEY",
        .baidu_secret_key = "YOUR_BAIDU_ACCESS_TOKEN",
        .deepseek_api_key = "YOUR_DEEPSEEK_API_KEY",
        .sample_rate = 16000,
        .chunk_ms = 1500,
        .serial_baud = 115200,
    };

    speech_pipeline_init(&cfg);
    speech_pipeline_process();
}

void app_main(void)
{
    xiaozhi_board_init();
    xTaskCreate(pipeline_task, "pipeline_task", 8192, NULL, 5, NULL);
}
