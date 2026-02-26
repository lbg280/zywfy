#include <Arduino.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

/*
 * 简化版示例：
 * 1) ESP32-C3 采集音频部分请替换为你自己的 I2S 麦克风录音实现。
 * 2) 录音数据必须是 WAV(16k,16bit,mono)。
 * 3) 本示例重点演示与后端接口交互（百度ASR + DeepSeek翻译）。
 */

static const char *WIFI_SSID = "YOUR_WIFI_SSID";
static const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Python 服务地址
static const char *SERVER_HOST = "192.168.1.100";
static const uint16_t SERVER_PORT = 8000;

WiFiClient wifi;
HttpClient http(wifi, SERVER_HOST, SERVER_PORT);

// TODO: 替换成你的真实 WAV 数据（可由 I2S 录音生成）
extern const uint8_t SAMPLE_WAV[];
extern const size_t SAMPLE_WAV_LEN;

String postAudioAndGetEnglish(const uint8_t *wavData, size_t wavLen) {
  const String boundary = "----esp32c3Boundary";
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  head += "Content-Type: audio/wav\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = head.length() + wavLen + tail.length();

  http.beginRequest();
  http.post("/translate_audio");
  http.sendHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.sendHeader("Content-Length", totalLen);
  http.beginBody();

  http.print(head);
  http.write(wavData, wavLen);
  http.print(tail);

  int statusCode = http.responseStatusCode();
  String response = http.responseBody();

  if (statusCode != 200) {
    Serial.printf("HTTP %d: %s\n", statusCode, response.c_str());
    return "";
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("JSON parse failed: %s\n", err.c_str());
    return "";
  }

  String zh = doc["zh_text"] | "";
  String en = doc["en_text"] | "";

  Serial.println("识别中文: " + zh);
  Serial.println("英文翻译: " + en);

  return en;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi OK, IP=");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  connectWiFi();

  Serial.println("开始发送测试音频...");
  String translated = postAudioAndGetEnglish(SAMPLE_WAV, SAMPLE_WAV_LEN);

  if (translated.length() == 0) {
    Serial.println("翻译失败，请检查服务日志。");
  } else {
    Serial.println("翻译完成。可在此处接入TTS播报逻辑。");
  }
}

void loop() {
  delay(5000);
}

// 示例占位音频（必须替换）
const uint8_t SAMPLE_WAV[] = {0x52, 0x49, 0x46, 0x46};
const size_t SAMPLE_WAV_LEN = sizeof(SAMPLE_WAV);
