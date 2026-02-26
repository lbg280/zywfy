#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <driver/i2s.h>

/*
 * xiaozhi-esp32-c3 中文 -> 英文翻译（按键触发）
 * - 按键按下开始录音，松开结束
 * - 录音生成 WAV(16k/16bit/mono)
 * - 上传到 /translate_audio_raw
 * - 打印中文识别和英文翻译
 *
 * 说明：以下 I2S 引脚是示例，请按你的硬件实际连线修改。
 */

static const char *WIFI_SSID = "YOUR_WIFI_SSID";
static const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";

static const char *SERVER_URL = "http://192.168.1.100:8000/translate_audio_raw";

// 按键：按下为 LOW（INPUT_PULLUP）
static constexpr int BUTTON_PIN = 9;

// I2S 麦克风引脚（示例，按实际连线修改）
static constexpr int I2S_BCLK = 4;
static constexpr int I2S_WS = 5;
static constexpr int I2S_DIN = 6;

static constexpr int SAMPLE_RATE = 16000;
static constexpr int RECORD_SECONDS_MAX = 8;
static constexpr size_t PCM_SAMPLES_MAX = SAMPLE_RATE * RECORD_SECONDS_MAX;

static int16_t *g_pcm = nullptr;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WiFi] connecting");
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
    if (millis() - startMs > 20000) {
      Serial.println("\n[WiFi] timeout, retry in loop");
      return;
    }
  }

  Serial.printf("\n[WiFi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
}

bool initI2S() {
  i2s_config_t i2s_config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK,
      .ws_io_num = I2S_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_DIN,
  };

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("[I2S] install failed: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[I2S] set pin failed: %d\n", err);
    return false;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("[I2S] init ok");
  return true;
}

size_t recordPcmWhilePressed(int16_t *pcmOut, size_t maxSamples) {
  size_t totalSamples = 0;
  int32_t rawBuf[256];

  Serial.println("[REC] recording... release button to stop");
  while (digitalRead(BUTTON_PIN) == LOW && totalSamples < maxSamples) {
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_NUM_0, rawBuf, sizeof(rawBuf), &bytesRead, pdMS_TO_TICKS(200));
    if (err != ESP_OK || bytesRead == 0) {
      continue;
    }

    size_t samples32 = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samples32 && totalSamples < maxSamples; ++i) {
      // I2S 32bit 数据转换到 16bit PCM
      int32_t s = rawBuf[i] >> 14;
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      pcmOut[totalSamples++] = static_cast<int16_t>(s);
    }

    delay(1);
  }

  Serial.printf("[REC] done, samples=%u\n", static_cast<unsigned>(totalSamples));
  return totalSamples;
}

size_t buildWavFromPcm(const int16_t *pcm, size_t pcmSamples, uint8_t *wavOut, size_t wavCap) {
  size_t dataSize = pcmSamples * sizeof(int16_t);
  size_t wavSize = 44 + dataSize;
  if (wavCap < wavSize) return 0;

  uint8_t *p = wavOut;
  memcpy(p, "RIFF", 4); p += 4;
  uint32_t riffChunkSize = static_cast<uint32_t>(wavSize - 8);
  memcpy(p, &riffChunkSize, 4); p += 4;
  memcpy(p, "WAVE", 4); p += 4;

  memcpy(p, "fmt ", 4); p += 4;
  uint32_t fmtChunkSize = 16; memcpy(p, &fmtChunkSize, 4); p += 4;
  uint16_t audioFormat = 1; memcpy(p, &audioFormat, 2); p += 2;
  uint16_t channels = 1; memcpy(p, &channels, 2); p += 2;
  uint32_t sampleRate = SAMPLE_RATE; memcpy(p, &sampleRate, 4); p += 4;
  uint32_t byteRate = SAMPLE_RATE * channels * 2; memcpy(p, &byteRate, 4); p += 4;
  uint16_t blockAlign = channels * 2; memcpy(p, &blockAlign, 2); p += 2;
  uint16_t bitsPerSample = 16; memcpy(p, &bitsPerSample, 2); p += 2;

  memcpy(p, "data", 4); p += 4;
  uint32_t subChunk2Size = static_cast<uint32_t>(dataSize); memcpy(p, &subChunk2Size, 4); p += 4;

  memcpy(p, pcm, dataSize);
  return wavSize;
}

bool uploadWavAndPrintResult(const uint8_t *wav, size_t wavLen) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[NET] wifi not connected");
      return false;
    }
  }

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(30000);

  if (!http.begin(SERVER_URL)) {
    Serial.println("[HTTP] begin failed");
    return false;
  }

  http.addHeader("Content-Type", "audio/wav");
  int code = http.POST(wav, wavLen);
  String body = http.getString();
  http.end();

  if (code != 200) {
    Serial.printf("[HTTP] code=%d body=%s\n", code, body.c_str());
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[JSON] parse error: %s\n", err.c_str());
    return false;
  }

  const char *zh = doc["zh_text"] | "";
  const char *en = doc["en_text"] | "";

  Serial.printf("[ASR] zh: %s\n", zh);
  Serial.printf("[NMT] en: %s\n", en);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(600);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  g_pcm = static_cast<int16_t *>(heap_caps_malloc(PCM_SAMPLES_MAX * sizeof(int16_t), MALLOC_CAP_8BIT));
  if (!g_pcm) {
    Serial.println("[ERR] alloc pcm buffer failed");
    return;
  }

  connectWiFi();
  if (!initI2S()) {
    Serial.println("[ERR] i2s init failed");
    return;
  }

  Serial.println("[READY] hold button to record and translate");
}

void loop() {
  bool nowPressed = (digitalRead(BUTTON_PIN) == LOW);
  if (nowPressed) {
    size_t pcmSamples = recordPcmWhilePressed(g_pcm, PCM_SAMPLES_MAX);

    if (pcmSamples < SAMPLE_RATE / 2) {
      Serial.println("[REC] too short, ignored");
    } else {
      size_t wavCap = 44 + pcmSamples * sizeof(int16_t);
      uint8_t *wavBuf = static_cast<uint8_t *>(heap_caps_malloc(wavCap, MALLOC_CAP_8BIT));
      if (!wavBuf) {
        Serial.println("[ERR] alloc wav buffer failed");
      } else {
        size_t wavLen = buildWavFromPcm(g_pcm, pcmSamples, wavBuf, wavCap);
        if (wavLen == 0) {
          Serial.println("[ERR] build wav failed");
        } else {
          uploadWavAndPrintResult(wavBuf, wavLen);
        }
        free(wavBuf);
      }
    }

    Serial.println("[READY] hold button to record and translate");
    delay(300);
  }

  delay(20);
}
