#include <Arduino.h>
#include "esp_camera.h"
#include <Arduino_GFX_Library.h>
#include <NimBLEDevice.h>

#define TFT_SCK   39
#define TFT_MOSI  38
#define TFT_CS    40
#define TFT_DC    41
#define TFT_RST   42
#define TFT_ROTATION 3
#define TFT_MADCTL 0x60
#define TFT_IPS   true

#define BTN_PIN   0

#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   15
#define SIOD_GPIO_NUM    4
#define SIOC_GPIO_NUM    5
#define Y9_GPIO_NUM     16
#define Y8_GPIO_NUM     17
#define Y7_GPIO_NUM     18
#define Y6_GPIO_NUM     12
#define Y5_GPIO_NUM     10
#define Y4_GPIO_NUM      8
#define Y3_GPIO_NUM      9
#define Y2_GPIO_NUM     11
#define VSYNC_GPIO_NUM   6
#define HREF_GPIO_NUM    7
#define PCLK_GPIO_NUM   13

#define CAM_HMIRROR 0
#define CAM_VFLIP   1
#define JPEG_QUALITY 60

#define BLE_NAME      "ESP32-CAM-AI"
#define SERVICE_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define RX_UUID       "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define TX_UUID       "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, TFT_ROTATION, TFT_IPS, 240, 320);

#define SCR_W 320
#define SCR_H 240

uint16_t *smallBuf = nullptr;

enum State { PREVIEW, WAITING, RESULT };
State state = PREVIEW;
uint32_t waitStart = 0;

NimBLECharacteristic *txChar = nullptr;
volatile bool bleConnected = false;
volatile uint16_t bleMtu = 23;

String rxBuf, resultText, statusText;
volatile bool resultReady = false;
volatile bool statusReady = false;

void handleRx(const uint8_t *d, size_t n) {
  if (n < 1) return;
  uint8_t t = d[0];
  if (t == 0x01) {
    rxBuf = "";
  } else if (t == 0x02) {
    for (size_t i = 1; i < n; i++) rxBuf += (char)d[i];
  } else if (t == 0x03) {
    resultText = rxBuf;
    resultReady = true;
  } else if (t == 0x04) {
    String s;
    for (size_t i = 1; i < n; i++) s += (char)d[i];
    statusText = s;
    statusReady = true;
  }
}

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
    bleConnected = true;
    Serial.println("BLE: da ket noi");
  }
  void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override {
    bleConnected = false;
    bleMtu = 23;
    Serial.println("BLE: ngat ket noi");
    NimBLEDevice::getAdvertising()->start();
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo &info) override {
    bleMtu = mtu;
    Serial.printf("BLE: MTU = %u\n", mtu);
  }
};

class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
    NimBLEAttValue v = c->getValue();
    handleRx(v.data(), v.length());
  }
};

void bleInit() {
  NimBLEDevice::init(BLE_NAME);
  NimBLEDevice::setMTU(517);
  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  NimBLEService *svc = server->createService(SERVICE_UUID);
  txChar = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic *rxChar =
      svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCB());
  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->enableScanResponse(true);
  adv->start();
}

bool sendPacket(const uint8_t *d, size_t n) {
  for (int i = 0; i < 100; i++) {
    if (!bleConnected) return false;
    if (txChar->notify(d, n)) return true;
    delay(5);
  }
  return false;
}

void banner(const String &msg, uint16_t color = RGB565_YELLOW) {
  gfx->fillRect(0, SCR_H - 26, SCR_W, 26, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(color);
  gfx->setCursor(6, SCR_H - 21);
  gfx->print(msg);
}

void drawWrapped(const String &s) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_CYAN);
  gfx->setCursor(6, 4);
  gfx->print("Gemini:");
  gfx->drawFastHLine(0, 24, SCR_W, RGB565_CYAN);

  gfx->setTextColor(RGB565_WHITE);
  const int cols = 26, lineH = 18, maxLines = 11;
  int y = 30, lines = 0;
  String line = "", word = "";
  bool truncated = false;

  auto flush = [&]() {
    if (lines >= maxLines) { truncated = true; return; }
    gfx->setCursor(6, y);
    gfx->print(line);
    y += lineH;
    lines++;
    line = "";
  };

  String text = s + " ";
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == ' ' || c == '\n') {
      if (word.length()) {
        while (word.length() > (size_t)cols) {
          if (line.length()) flush();
          line = word.substring(0, cols);
          word = word.substring(cols);
          flush();
        }
        if (line.length() + word.length() + (line.length() ? 1 : 0) > (size_t)cols) flush();
        line += (line.length() ? " " : "") + word;
        word = "";
      }
      if (c == '\n' && line.length()) flush();
    } else if (c >= 32 && c < 127) {
      word += c;
    } else {
      word += '?';
    }
    if (truncated) break;
  }
  if (!truncated && line.length()) flush();
  if (truncated) { gfx->setCursor(6, SCR_H - 20); gfx->print("..."); }

  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setCursor(6, SCR_H - 10);
  gfx->print("Nhan BOOT de chup lai");
}

bool cameraInit() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size = FRAMESIZE_VGA;
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init loi 0x%x\n", err);
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_hmirror(s, CAM_HMIRROR);
    s->set_vflip(s, CAM_VFLIP);
  }
  return true;
}

void drawPreview() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;
  const uint16_t *src = (const uint16_t *)fb->buf;
  int W = fb->width, H = fb->height;
  int sx = W / SCR_W, sy = H / SCR_H;
  if (sx < 1) sx = 1;
  if (sy < 1) sy = 1;
  for (int y = 0; y < SCR_H; y++) {
    const uint16_t *row = src + (size_t)(y * sy) * W;
    uint16_t *dst = smallBuf + y * SCR_W;
    for (int x = 0; x < SCR_W; x++) dst[x] = row[x * sx];
  }
  esp_camera_fb_return(fb);
  gfx->draw16bitBeRGBBitmap(0, 0, smallBuf, SCR_W, SCR_H);
  gfx->fillCircle(306, 14, 6, bleConnected ? RGB565_GREEN : RGB565_RED);
}

bool sendImage(const uint8_t *data, size_t len) {
  size_t payload = (bleMtu > 3 ? bleMtu - 3 : 20);
  if (payload > 240) payload = 240;
  payload -= 1;

  uint8_t pkt[256];
  uint32_t len32 = (uint32_t)len;
  pkt[0] = 0x01;
  memcpy(pkt + 1, &len32, 4);
  if (!sendPacket(pkt, 5)) return false;

  size_t off = 0;
  int lastPct = -10;
  while (off < len) {
    size_t n = min(payload, len - off);
    pkt[0] = 0x02;
    memcpy(pkt + 1, data + off, n);
    if (!sendPacket(pkt, n + 1)) return false;
    off += n;
    int pct = (int)(off * 100 / len);
    if (pct >= lastPct + 10) {
      lastPct = pct;
      banner("Dang gui anh " + String(pct) + "%");
    }
    delay(3);
  }
  pkt[0] = 0x03;
  return sendPacket(pkt, 1);
}

void captureAndSend() {
  if (!bleConnected) {
    banner("Chua noi BLE dien thoai!", RGB565_RED);
    delay(1200);
    return;
  }
  banner("Dang chup...");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    banner("Loi camera!", RGB565_RED);
    delay(1000);
    return;
  }
  uint8_t *jpg = nullptr;
  size_t jlen = 0;
  bool ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format, JPEG_QUALITY, &jpg, &jlen);
  esp_camera_fb_return(fb);
  if (!ok || !jpg) {
    banner("Loi nen JPEG!", RGB565_RED);
    delay(1000);
    return;
  }
  Serial.printf("JPEG: %u bytes\n", (unsigned)jlen);

  resultReady = false;
  statusReady = false;
  bool sent = sendImage(jpg, jlen);
  free(jpg);
  if (!sent) {
    banner("Gui anh that bai!", RGB565_RED);
    delay(1200);
    return;
  }
  state = WAITING;
  waitStart = millis();
  banner("Dang cho Gemini...");
}

bool buttonEvent() {
  static bool last = HIGH;
  static uint32_t t = 0;
  bool cur = digitalRead(BTN_PIN);
  if (cur != last && millis() - t > 40) {
    t = millis();
    last = cur;
    if (cur == LOW) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  gfx->begin(40000000);
#if TFT_MADCTL >= 0
  bus->beginWrite();
  bus->writeCommand(0x36);
  bus->write((uint8_t)TFT_MADCTL);
  bus->endWrite();
#endif
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(6, 6);
  gfx->println("Khoi dong...");

  if (!psramFound()) {
    gfx->setTextColor(RGB565_RED);
    gfx->println("Khong thay PSRAM!");
    gfx->println("Chon PSRAM: OPI PSRAM");
    while (true) delay(1000);
  }
  smallBuf = (uint16_t *)ps_malloc(SCR_W * SCR_H * 2);

  if (!cameraInit()) {
    gfx->setTextColor(RGB565_RED);
    gfx->println("Loi camera!");
    gfx->println("Kiem tra chan/cap FPC");
    while (true) delay(1000);
  }

  bleInit();
  Serial.println("San sang. Ten BLE: " BLE_NAME);
}

void loop() {
  bool btn = buttonEvent();

  switch (state) {
    case PREVIEW:
      drawPreview();
      if (btn) captureAndSend();
      break;

    case WAITING:
      if (statusReady) {
        statusReady = false;
        banner(statusText);
      }
      if (resultReady) {
        resultReady = false;
        drawWrapped(resultText);
        state = RESULT;
      } else if (btn) {
        state = PREVIEW;
      } else if (millis() - waitStart > 90000) {
        banner("Het thoi gian cho!", RGB565_RED);
        delay(1500);
        state = PREVIEW;
      }
      if (!bleConnected) {
        banner("Mat ket noi BLE!", RGB565_RED);
        delay(1500);
        state = PREVIEW;
      }
      delay(20);
      break;

    case RESULT:
      if (btn) state = PREVIEW;
      delay(20);
      break;
  }
}
