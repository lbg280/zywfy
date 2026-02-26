# ESP32-C3 中文语音转英语实现（参考 xiaozhi-esp32 架构）

由于当前环境无法直接访问 `https://github.com/78/xiaozhi-esp32`（网络 403），这里给出一套可直接落地在 ESP-IDF 的实现，并按 xiaozhi-esp32 常见语音链路拆分：

1. 麦克风采集 16k PCM。
2. VAD 切分一句话。
3. 将整句上传 ASR 获取中文文本。
4. 将中文文本上传翻译服务获取英文文本。
5. 回调英文文本（可继续接入 TTS 或串口输出）。

> 目录 `esp32_c3_zh2en/main` 中是完整示例代码。你可以把 `voice_translate_app.[ch]` 合并到现有工程，再把 I2S 回调中的 PCM 数据喂给 `voice_translate_feed_pcm()`。
