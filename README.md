# xiaozhi-esp32-c3 中英文实时翻译（百度语音 + DeepSeek）

这是一个可直接落地的完整示例：
- 设备端：`xiaozhi-esp32-c3`（Arduino / PlatformIO）
- 语音识别：百度 ASR
- 文本翻译：DeepSeek 大模型
- （可选）英文语音合成：百度 TTS

> 当前工程分为两部分：
> 1. `server/`：Python 后端，负责百度 ASR + DeepSeek 翻译 + 百度 TTS。
> 2. `firmware/`：ESP32-C3 端，上传 WAV 录音并接收翻译结果。

---

## 1. 后端部署（必须先完成）

### 1.1 准备环境
```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 1.2 配置密钥
```bash
cp ../.env.example ../.env
# 编辑 .env，填入 DeepSeek 和百度语音密钥
```

`.env` 示例：
```env
DEEPSEEK_API_KEY=sk-xxxx
DEEPSEEK_MODEL=deepseek-chat
BAIDU_API_KEY=xxxx
BAIDU_SECRET_KEY=xxxx
HOST=0.0.0.0
PORT=8000
```

### 1.3 启动服务
```bash
cd ..
source server/.venv/bin/activate
uvicorn server.app:app --host 0.0.0.0 --port 8000
```

接口：
- `GET /health`
- `POST /translate_audio`（form-data 文件字段名：`file`，WAV）

---

## 2. ESP32-C3 固件

### 2.1 修改配置
编辑 `firmware/src/main.cpp`：
- `WIFI_SSID`
- `WIFI_PASS`
- `SERVER_HOST`（改成你电脑/服务器在局域网的 IP）

### 2.2 编译烧录
```bash
cd firmware
pio run -t upload
pio device monitor
```

### 2.3 录音逻辑说明
`main.cpp` 里保留了 `SAMPLE_WAV` 占位数据，**你需要替换成 I2S 麦克风录音结果**，要求：
- 格式：WAV
- 采样率：16k
- 单声道
- 16bit

你可以把你已有的 xiaozhi-esp32-c3 录音函数接入到 `postAudioAndGetEnglish(...)` 调用前。

---

## 3. 请求与返回格式

### 请求
`POST /translate_audio`
- `multipart/form-data`
- 字段：`file`（audio.wav）

### 返回 JSON
```json
{
  "zh_text": "今天天气真好",
  "en_text": "The weather is really nice today.",
  "tts_audio_base64": "...",
  "tts_md5": "...",
  "tts_format": "mp3"
}
```

---

## 4. 常见问题

1. **ASR 失败**：请确认上传的是标准 WAV（16k/16bit/mono）。
2. **DeepSeek 报错**：确认 API Key 与模型名可用。
3. **ESP32 连接失败**：确认 `SERVER_HOST` 可被开发板访问。
4. **想要播报英文**：可在 ESP32 端把 `tts_audio_base64` 解码后送到音频播放链路（I2S DAC/功放）。

---

## 5. 后续可扩展
- 按键按下开始录音、松开结束并上传
- WebSocket 流式识别 + 流式翻译
- 本地 VAD 降噪（减少无效上传）
