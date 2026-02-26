# ESP32-C3 语音识别 + 中文翻译方案（百度语音 + DeepSeek）

本仓库提供了一个可落地的最小实现骨架，满足以下目标：

1. **实时聆听外界语音**（通过小智现有麦克风驱动采集 PCM）
2. **语音识别**（百度语音 ASR）
3. **翻译成中文**（DeepSeek 大模型）
4. **串口输出结果**（识别文本 + 翻译文本）

## 与小智源码兼容策略

代码中显式保留了小智驱动接口，不改原硬件驱动：

- `xiaozhi_board_init()`：板级初始化
- `xiaozhi_mic_read()`：读取麦克风 PCM
- `xiaozhi_vad_is_speech()`：语音活动检测（VAD）

你只需要把这三个符号映射到小智项目现有实现，即可完成对接。

## 核心流程

```text
麦克风 PCM -> VAD 过滤 -> 百度 ASR -> DeepSeek 翻译 -> 串口输出
```

## 目录结构

- `main/app_main.c`：系统入口，初始化板级驱动并启动流水线任务
- `components/speech_pipeline/speech_pipeline.c`：语音识别与翻译主流程
- `components/speech_pipeline/http_client_helper.c`：HTTP POST 封装
- `components/speech_pipeline/include/speech_pipeline.h`：对外 API

## 注意事项（上线前必须补齐）

1. `encode_pcm_to_base64()` 当前是占位实现，需替换为真实 Base64。
2. 百度语音中 `token` 建议通过 OAuth 流程动态获取并刷新，示例中使用配置项占位。
3. 建议增加：
   - 端侧降噪/AGC
   - 更稳健的分段（静音切分 + 句子拼接）
   - 网络失败重试与熔断
4. 若要改为屏幕显示，可在 `speech_pipeline_feed_pcm()` 内把 `printf` 改为显示驱动输出。

## 精度与语义偏差控制建议

- 采样率固定 `16k/16bit/mono`
- 使用 VAD 仅上传语音段，减少噪声误识别
- DeepSeek system prompt 固定为“仅翻译，不解释”
- 对专有名词可增加词表后处理

## 快速移植步骤

1. 在小智工程中引入本组件。
2. 将 `extern` 的三个 `xiaozhi_*` 接口绑定到现有实现。
3. 填入 `Baidu` 与 `DeepSeek` 的密钥/令牌。
4. 编译烧录后查看串口日志。

