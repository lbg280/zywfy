# xiaozhi-esp32-c3 中英文翻译（百度语音 + DeepSeek）

这版是可直接跑在开发板上的“按键录音 -> 中文识别 -> 英文翻译”完整实现，修复了之前示例里占位 WAV 导致无法实机使用的问题。

- 开发板：ESP32-C3（PlatformIO / Arduino）
- 语音识别：百度 ASR
- 翻译：DeepSeek
- 英文语音：百度 TTS（后端返回 base64 mp3，可选接播放）

## 目录
- `server/`: Python 后端
- `firmware/`: ESP32-C3 固件

---

## 1) 后端部署

### 1.1 安装依赖
```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### 1.2 配置密钥
```bash
cd ..
cp .env.example .env
# 编辑 .env 填入密钥
```

### 1.3 启动
```bash
source server/.venv/bin/activate
uvicorn server.app:app --host 0.0.0.0 --port 8000
```

### 1.4 接口
- `GET /health`
- `POST /translate_audio`：支持 multipart 上传（字段名 `file`）或直接 body 上传 wav
- `POST /translate_audio_raw`：直接 body 上传 wav（固件默认使用）

---

## 2) 固件使用

### 2.1 修改配置（必须）
编辑 `firmware/src/main.cpp`：
- `WIFI_SSID`, `WIFI_PASS`
- `SERVER_URL`（改成你的后端地址，如 `http://192.168.1.100:8000/translate_audio_raw`）
- I2S 引脚：`I2S_BCLK`, `I2S_WS`, `I2S_DIN`
- 按键引脚：`BUTTON_PIN`

> 当前按键逻辑使用 `INPUT_PULLUP`，即按下为 LOW。

### 2.2 编译烧录
```bash
cd firmware
pio run -t upload
pio device monitor
```

### 2.3 使用方式
1. 串口看到 `[READY] hold button to record and translate`
2. 按住按键说中文
3. 松开按键后自动上传并翻译
4. 串口输出：
   - `[ASR] zh: ...`
   - `[NMT] en: ...`

---

## 3) 解决你反馈的重启问题

你提供的日志是持续 `RTC_SW_SYS_RST` 重启。这个版本避免了旧示例中的高风险点：

- 去掉占位假 WAV 自动上传逻辑，改成“按键触发真实录音”
- 使用 I2S 实时采集，构造标准 WAV 头
- 网络请求放在明确流程里，并加入超时
- 启动阶段仅初始化，不做危险的无效请求

如果仍重启，请优先检查：
- 板型是否正确（`esp32-c3-devkitm-1`）
- USB 供电是否稳定
- I2S 引脚是否与实际硬件一致
- 按键引脚是否冲突启动脚

---

## 4) 返回格式

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

## 5) 可继续扩展
- 把 `tts_audio_base64` 在设备端解码后播放
- 加入 VAD 降噪
- 改 WebSocket 流式识别/翻译
