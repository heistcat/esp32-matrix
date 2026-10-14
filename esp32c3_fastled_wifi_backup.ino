#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_coexist.h>
#include "ble_app_html.h"

#define LED_PIN         8
#define NUM_LEDS        100
#define MATRIX_W        20
#define MATRIX_H        5
#define MAX_ANIM_FRAMES 16

CRGB leds[NUM_LEDS];
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

// --- Режимы работы устройства ---
enum AppMode {
  MODE_TEXT = 0,
  MODE_EFFECT = 1,
  MODE_CUSTOM_ANIM = 2,
  MODE_DRAW_LIVE = 3,
  MODE_RAW_STREAM = 4
};

AppMode appMode = MODE_TEXT;

// --- Состояние бегущей строки V2 ---
String currentText = "HELLO WORLD!";
uint8_t brightness = 40;
uint16_t scrollDelay = 65;      // задержка скролла в мс
uint8_t textColMode = 0;        // 0..9 режимов окраски
CRGB color1 = CRGB(0, 255, 255);// Cyan
CRGB color2 = CRGB(255, 0, 128);// Pink
CRGB color3 = CRGB(255, 230, 0);// Yellow neon
uint8_t textStyle = 0;          // 0:Скролл, 1:Машинка, 2:Волна Y, 3:Дыхание, 4:Глитч, 5:Статичный
uint8_t textBgMode = 0;         // 0:Черный, 1:Звезды, 2:Огонь, 3:Плазма
bool textRunning = true;
bool seamlessLoop = true;        // бесшовное зацикливание без пустого экрана
uint8_t loopGap = 3;             // отступ между повторами в пикселях (0 = встык)

uint8_t textBuffer[1024];
int textLength = 0;
int scrollPos = 0;
uint8_t baseHue = 0;
unsigned long lastTextStep = 0;

// --- BLE (Nordic UART Service) & Безопасность ---
#define BLE_SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pBleServer = NULL;
BLECharacteristic *pBleTxChar = NULL;
bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;
bool bleAuthenticated = false;
String blePassword = "1234";
String bleRxBuffer = "";

// --- Состояние встроенных эффектов ---
uint8_t currentEffect = 0;      // 0: Nyan Cat, 1: Matrix, 2: Fire, 3: Equalizer, 4: Pacman, 5: Heart, 6: Plasma
unsigned long lastEffectStep = 0;
uint16_t effectSpeed = 55;      // скорость воспроизведения эффектов в мс

// --- Состояние пользовательской покадровой анимации ---
CRGB customFrames[MAX_ANIM_FRAMES][NUM_LEDS];
uint8_t customFrameCount = 0;
uint8_t customCurrentFrame = 0;
uint8_t customFps = 6;
bool customAnimPlaying = false;
unsigned long lastCustomFrameTime = 0;

// Буфер для режима живого рисования
CRGB liveDrawBuffer[NUM_LEDS];

// --- Маппинг координат 20x5 (4 сегмента 5x5, горизонтальная змейка снизу вверх) ---
uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_W || y >= MATRIX_H) return 0;
  uint8_t panel = x / 5;
  uint8_t localX = x % 5;
  uint8_t localY = 4 - y;
  uint8_t localIndex = (localY % 2 == 0) ? (localY * 5 + localX) : (localY * 5 + (4 - localX));
  return (panel * 25) + localIndex;
}

// --- 5-пиксельный растровый шрифт (ASCII + Кириллица UTF-8 + 50+ Эмодзи) ---
uint8_t getGlyphCols(uint32_t u, uint8_t* out) {
  if (u >= 'a' && u <= 'z') u -= 32;
  if (u >= 0x0430 && u <= 0x044F) u -= 0x20;
  if (u == 0x0401 || u == 0x0451) u = 0x0415; // Ё -> Е

  // Буквы кириллицы, визуально идентичные латинице:
  if (u == 0x0410) u = 'A';
  else if (u == 0x0412) u = 'B';
  else if (u == 0x0415) u = 'E';
  else if (u == 0x041A) u = 'K';
  else if (u == 0x041C) u = 'M';
  else if (u == 0x041D) u = 'H';
  else if (u == 0x041E) u = 'O';
  else if (u == 0x0420) u = 'P';
  else if (u == 0x0421) u = 'C';
  else if (u == 0x0422) u = 'T';
  else if (u == 0x0423) u = 'Y';
  else if (u == 0x0425) u = 'X';

  switch (u) {
    // --- Латиница ---
    case 'A': out[0]=0x1E; out[1]=0x05; out[2]=0x05; out[3]=0x1E; return 4;
    case 'B': out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x0A; return 4;
    case 'C': out[0]=0x0E; out[1]=0x11; out[2]=0x11; out[3]=0x11; return 4;
    case 'D': out[0]=0x1F; out[1]=0x11; out[2]=0x11; out[3]=0x0E; return 4;
    case 'E': out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x11; return 4;
    case 'F': out[0]=0x1F; out[1]=0x05; out[2]=0x05; out[3]=0x01; return 4;
    case 'G': out[0]=0x0E; out[1]=0x11; out[2]=0x15; out[3]=0x1D; return 4;
    case 'H': out[0]=0x1F; out[1]=0x04; out[2]=0x04; out[3]=0x1F; return 4;
    case 'I': out[0]=0x11; out[1]=0x1F; out[2]=0x11; return 3;
    case 'J': out[0]=0x08; out[1]=0x10; out[2]=0x10; out[3]=0x1F; return 4;
    case 'K': out[0]=0x1F; out[1]=0x04; out[2]=0x0A; out[3]=0x11; return 4;
    case 'L': out[0]=0x1F; out[1]=0x10; out[2]=0x10; out[3]=0x10; return 4;
    case 'M': out[0]=0x1F; out[1]=0x02; out[2]=0x04; out[3]=0x02; out[4]=0x1F; return 5;
    case 'N': out[0]=0x1F; out[1]=0x02; out[2]=0x0C; out[3]=0x1F; return 4;
    case 'O': out[0]=0x0E; out[1]=0x11; out[2]=0x11; out[3]=0x0E; return 4;
    case 'P': out[0]=0x1F; out[1]=0x05; out[2]=0x05; out[3]=0x02; return 4;
    case 'Q': out[0]=0x0E; out[1]=0x11; out[2]=0x11; out[3]=0x0E; out[4]=0x10; return 5;
    case 'R': out[0]=0x1F; out[1]=0x05; out[2]=0x0D; out[3]=0x16; return 4;
    case 'S': out[0]=0x12; out[1]=0x15; out[2]=0x15; out[3]=0x09; return 4;
    case 'T': out[0]=0x01; out[1]=0x01; out[2]=0x1F; out[3]=0x01; out[4]=0x01; return 5;
    case 'U': out[0]=0x0F; out[1]=0x10; out[2]=0x10; out[3]=0x0F; return 4;
    case 'V': out[0]=0x07; out[1]=0x18; out[2]=0x10; out[3]=0x18; out[4]=0x07; return 5;
    case 'W': out[0]=0x1F; out[1]=0x08; out[2]=0x04; out[3]=0x08; out[4]=0x1F; return 5;
    case 'X': out[0]=0x11; out[1]=0x0A; out[2]=0x04; out[3]=0x0A; out[4]=0x11; return 5;
    case 'Y': out[0]=0x03; out[1]=0x04; out[2]=0x18; out[3]=0x04; out[4]=0x03; return 5;
    case 'Z': out[0]=0x19; out[1]=0x15; out[2]=0x15; out[3]=0x13; return 4;

    // --- Кириллица ---
    case 0x0411: out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x0D; return 4; // Б
    case 0x0413: out[0]=0x1F; out[1]=0x01; out[2]=0x01; out[3]=0x01; return 4; // Г
    case 0x0414: out[0]=0x18; out[1]=0x0F; out[2]=0x09; out[3]=0x0F; out[4]=0x18; return 5; // Д
    case 0x0416: out[0]=0x11; out[1]=0x0A; out[2]=0x1F; out[3]=0x0A; out[4]=0x11; return 5; // Ж (Х с палкой по центру)
    case 0x0417: out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x0A; return 4; // З
    case 0x0418: out[0]=0x1F; out[1]=0x08; out[2]=0x04; out[3]=0x02; out[4]=0x1F; return 5; // И (четкая 5x5 диагональ)
    case 0x0419: out[0]=0x1E; out[1]=0x08; out[2]=0x05; out[3]=0x02; out[4]=0x1E; return 5; // Й (5x5 с диакритикой)
    case 0x041B: out[0]=0x0F; out[1]=0x01; out[2]=0x01; out[3]=0x1F; return 4; // Л
    case 0x041F: out[0]=0x1F; out[1]=0x01; out[2]=0x01; out[3]=0x1F; return 4; // П
    case 0x0424: out[0]=0x04; out[1]=0x0E; out[2]=0x1F; out[3]=0x0E; out[4]=0x04; return 5; // Ф
    case 0x0426: out[0]=0x0F; out[1]=0x08; out[2]=0x08; out[3]=0x1F; out[4]=0x18; return 5; // Ц
    case 0x0427: out[0]=0x07; out[1]=0x04; out[2]=0x04; out[3]=0x1F; return 4; // Ч
    case 0x0428: out[0]=0x1F; out[1]=0x10; out[2]=0x1F; out[3]=0x10; out[4]=0x1F; return 5; // Ш
    case 0x0429: out[0]=0x0F; out[1]=0x08; out[2]=0x0F; out[3]=0x08; out[4]=0x1F; out[5]=0x18; return 6; // Щ
    case 0x042A: out[0]=0x01; out[1]=0x1F; out[2]=0x14; out[3]=0x14; out[4]=0x08; return 5; // Ъ
    case 0x042B: out[0]=0x1F; out[1]=0x14; out[2]=0x08; out[3]=0x00; out[4]=0x1F; return 5; // Ы
    case 0x042C: out[0]=0x1F; out[1]=0x14; out[2]=0x14; out[3]=0x08; return 4; // Ь
    case 0x042D: out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x0E; return 4; // Э
    case 0x042E: out[0]=0x1F; out[1]=0x04; out[2]=0x1F; out[3]=0x11; out[4]=0x1F; return 5; // Ю
    case 0x042F: out[0]=0x16; out[1]=0x0D; out[2]=0x05; out[3]=0x1F; return 4; // Я

    // --- Цифры ---
    case '0': out[0]=0x0E; out[1]=0x11; out[2]=0x11; out[3]=0x0E; return 4;
    case '1': out[0]=0x00; out[1]=0x1F; out[2]=0x00; return 3;
    case '2': out[0]=0x19; out[1]=0x15; out[2]=0x15; out[3]=0x12; return 4;
    case '3': out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x1F; return 4;
    case '4': out[0]=0x07; out[1]=0x04; out[2]=0x04; out[3]=0x1F; return 4;
    case '5': out[0]=0x17; out[1]=0x15; out[2]=0x15; out[3]=0x1D; return 4;
    case '6': out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x1D; return 4;
    case '7': out[0]=0x01; out[1]=0x19; out[2]=0x05; out[3]=0x03; return 4;
    case '8': out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x1F; return 4;
    case '9': out[0]=0x17; out[1]=0x15; out[2]=0x15; out[3]=0x1F; return 4;

    // --- Символы и знаки ---
    case ' ': out[0]=0x00; out[1]=0x00; return 2;
    case '!': out[0]=0x17; return 1;
    case '?': out[0]=0x01; out[1]=0x15; out[2]=0x03; return 3;
    case '.': out[0]=0x10; return 1;
    case ',': out[0]=0x18; return 1;
    case ':': out[0]=0x0A; return 1;
    case ';': out[0]=0x1A; return 1;
    case '-': out[0]=0x04; out[1]=0x04; out[2]=0x04; return 3;
    case '+': out[0]=0x04; out[1]=0x0E; out[2]=0x04; return 3;
    case '=': out[0]=0x0A; out[1]=0x0A; out[2]=0x0A; return 3;
    case '_': out[0]=0x10; out[1]=0x10; out[2]=0x10; return 3;
    case '|': out[0]=0x1F; return 1;
    case '~': out[0]=0x02; out[1]=0x01; out[2]=0x02; out[3]=0x01; return 4;
    case '^': out[0]=0x02; out[1]=0x01; out[2]=0x02; return 3;
    case '*': out[0]=0x0A; out[1]=0x04; out[2]=0x0A; return 3;
    case '/': out[0]=0x10; out[1]=0x08; out[2]=0x04; out[3]=0x02; out[4]=0x01; return 5; // Прямая 5x5 диагональ
    case '\\': out[0]=0x01; out[1]=0x02; out[2]=0x04; out[3]=0x08; out[4]=0x10; return 5; // Прямой обратный слэш
    case '$': out[0]=0x16; out[1]=0x15; out[2]=0x1F; out[3]=0x15; out[4]=0x0D; return 5; // $ (широкая S с прямой палкой)
    case '%': out[0]=0x13; out[1]=0x08; out[2]=0x04; out[3]=0x02; out[4]=0x19; return 5; // Процент
    case '@': out[0]=0x0E; out[1]=0x11; out[2]=0x17; out[3]=0x15; out[4]=0x0E; return 5; // @
    case '#': out[0]=0x0A; out[1]=0x1F; out[2]=0x0A; out[3]=0x1F; out[4]=0x0A; return 5; // #
    case '&': out[0]=0x0A; out[1]=0x15; out[2]=0x15; out[3]=0x0E; out[4]=0x10; return 5; // &
    case '(': out[0]=0x0E; out[1]=0x11; return 2;
    case ')': out[0]=0x11; out[1]=0x0E; return 2;
    case '[': out[0]=0x1F; out[1]=0x11; return 2;
    case ']': out[0]=0x11; out[1]=0x1F; return 2;
    case '{': out[0]=0x04; out[1]=0x0E; out[2]=0x11; return 3;
    case '}': out[0]=0x11; out[1]=0x0E; out[2]=0x04; return 3;
    case '<': out[0]=0x04; out[1]=0x0A; out[2]=0x11; return 3;
    case '>': out[0]=0x11; out[1]=0x0A; out[2]=0x04; return 3;

    // --- СТРЕЛКИ И УКАЗАТЕЛИ (5x5) ---
    case 0x2190: // ← Влево
      out[0]=0x04; out[1]=0x0E; out[2]=0x15; out[3]=0x04; out[4]=0x04; return 5;
    case 0x2192: // → Вправо
      out[0]=0x04; out[1]=0x04; out[2]=0x15; out[3]=0x0E; out[4]=0x04; return 5;
    case 0x2191: // ↑ Вверх
      out[0]=0x04; out[1]=0x02; out[2]=0x1F; out[3]=0x02; out[4]=0x04; return 5;
    case 0x2193: // ↓ Вниз
      out[0]=0x04; out[1]=0x08; out[2]=0x1F; out[3]=0x08; out[4]=0x04; return 5;
    case 0x2194: // ↔ Двусторонняя стрелка
      out[0]=0x0E; out[1]=0x15; out[2]=0x04; out[3]=0x15; out[4]=0x0E; return 5;
    case 0x00AB: // «
      out[0]=0x04; out[1]=0x0A; out[2]=0x04; out[3]=0x0A; return 4;
    case 0x00BB: // »
      out[0]=0x0A; out[1]=0x04; out[2]=0x0A; out[3]=0x04; return 4;

    // --- ОТОБРАННЫЕ ЧЕТКИЕ ЗНАЧКИ И СМАЙЛЫ (5x5) ---
    case 0x1F642: case 0x1F600: case 0x1F60A: case 0x1F603: case 0x1F604: // 🙂 Улыбка
      out[0]=0x08; out[1]=0x12; out[2]=0x10; out[3]=0x12; out[4]=0x08; return 5;
    case 0x2764: case 0x2665: case 0x1F496: // ❤️ Сердце
      out[0]=0x06; out[1]=0x0F; out[2]=0x1E; out[3]=0x0F; out[4]=0x06; return 5;
    case 0x1F319: case 0x1F31A: // 🌙 Луна
      out[0]=0x0E; out[1]=0x1F; out[2]=0x11; return 3;
    case 0x2705: case 0x2714: case 0x2713: // ✅ Галочка
      out[0]=0x08; out[1]=0x10; out[2]=0x08; out[3]=0x04; out[4]=0x02; return 5;
    case 0x274C: case 0x2716: case 0x26D4: // ❌ Крестик / Икс
      out[0]=0x11; out[1]=0x0A; out[2]=0x04; out[3]=0x0A; out[4]=0x11; return 5;

    default: out[0]=0x00; return 1;
  }
}

void prepareText(String str) {
  // Автоматическая замена текстовых смайликов и стрелок
  str.replace("<3", "❤️");
  str.replace(":-)", "🙂");
  str.replace(":)", "🙂");
  str.replace(":-D", "🙂");
  str.replace(":D", "🙂");
  str.replace("<--", "←");
  str.replace("<-", "←");
  str.replace("-->", "→");
  str.replace("->", "→");
  str.replace("<->", "↔");
  str.replace("<<", "«");
  str.replace(">>", "»");
  str.replace("^^", "↑");
  str.replace("^|", "↑");
  str.replace("v|", "↓");
  str.replace("[v]", "✅");
  str.replace("(v)", "✅");
  str.replace("[x]", "❌");
  str.replace("(x)", "❌");

  textLength = 0;
  int i = 0;
  int len = str.length();
  uint8_t temp[10];

  while (i < len && textLength < (int)(sizeof(textBuffer) - 12)) {
    uint8_t b = (uint8_t)str[i++];
    uint32_t unicode = 0;

    if ((b & 0x80) == 0) {
      unicode = b;
    } else if ((b & 0xE0) == 0xC0 && i < len) {
      uint8_t b2 = (uint8_t)str[i++];
      unicode = ((b & 0x1F) << 6) | (b2 & 0x3F);
    } else if ((b & 0xF0) == 0xE0 && i + 1 < len) {
      uint8_t b2 = (uint8_t)str[i++];
      uint8_t b3 = (uint8_t)str[i++];
      unicode = ((b & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    } else if ((b & 0xF8) == 0xF0 && i + 2 < len) {
      uint8_t b2 = (uint8_t)str[i++];
      uint8_t b3 = (uint8_t)str[i++];
      uint8_t b4 = (uint8_t)str[i++];
      unicode = ((b & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
    } else {
      continue;
    }

    if (unicode == 0xFE0F || unicode == 0xFE0E) continue; // Пропускаем вариаторы

    uint8_t w = getGlyphCols(unicode, temp);
    for (uint8_t j = 0; j < w; j++) {
      textBuffer[textLength++] = temp[j];
    }
    if (unicode != ' ') {
      textBuffer[textLength++] = 0x00;
    }
  }

  if (textLength == 0) {
    textBuffer[textLength++] = 0x00;
  }
}

CRGB parseHexColor(String hex) {
  if (hex.startsWith("#")) hex = hex.substring(1);
  if (hex.length() != 6) return CRGB::White;
  long number = strtol(hex.c_str(), NULL, 16);
  long r = (number >> 16) & 0xFF;
  long g = (number >> 8) & 0xFF;
  long b = number & 0xFF;
  return CRGB(r, g, b);
}

String colorToHex(CRGB c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
  return String(buf);
}

// ==========================================
// --- ДВИЖОК ЭФФЕКТОВ ТЕКСТА V2 ---
// ==========================================

CRGB getTextColor(uint8_t colMode, int x, uint8_t baseH, uint32_t stepTime) {
  switch (colMode) {
    case 0: // 🌈 Радуга
      return CHSV(baseH + x * 7, 240, 255);
    case 1: // 🎨 Монохром
      return color1;
    case 2: // 🌌 2-цветный градиент
      return blend(color1, color2, (x * 255) / (MATRIX_W - 1));
    case 3: // 🌅 3-цветный градиент (Color1 -> Color2 -> Color3)
      if (x < MATRIX_W / 2) {
        return blend(color1, color2, (x * 255) / (MATRIX_W / 2));
      } else {
        return blend(color2, color3, ((x - MATRIX_W / 2) * 255) / (MATRIX_W / 2));
      }
    case 4: { // 🔥 Огонь
      uint8_t heat = inoise8(x * 50 + baseH * 2, stepTime / 8);
      return ColorFromPalette(HeatColors_p, heat);
    }
    case 5: { // ⚡ Киберпанк / Неон (Циан -> Пурпур -> Неоновый желтый)
      uint8_t phase = (baseH * 2 + x * 15) % 255;
      if (phase < 85) return blend(CRGB(0, 240, 255), CRGB(255, 0, 128), phase * 3);
      else if (phase < 170) return blend(CRGB(255, 0, 128), CRGB(255, 230, 0), (phase - 85) * 3);
      else return blend(CRGB(255, 230, 0), CRGB(0, 240, 255), (phase - 170) * 3);
    }
    case 6: { // 🟢 Матрица (Зеленый терминал с белыми искрами)
      if ((x + (stepTime / 60)) % 5 == 0) return CRGB(220, 255, 230);
      return CRGB(0, 255, 50);
    }
    case 7: { // 🔮 Плазма
      uint8_t h = (sin8(x * 20 + baseH * 3) + cos8(x * 15 + stepTime / 10)) / 2;
      return CHSV(h, 230, 255);
    }
    case 8: { // ✨ Мерцание звезд / Shimmer
      uint8_t twinkle = inoise8(x * 80, stepTime / 5);
      return blend(color1, CRGB::White, (twinkle > 160) ? (twinkle - 160) * 2 : 0);
    }
    case 9: // 🍬 Пастель
      return CHSV(baseH + x * 8, 120, 255);
    default:
      return color1;
  }
}

void renderBackground(uint8_t bgMode, uint8_t baseH, uint32_t stepTime) {
  if (bgMode == 0) return; // чистый черный
  for (int x = 0; x < MATRIX_W; x++) {
    for (int y = 0; y < MATRIX_H; y++) {
      if (bgMode == 1) { // 🌌 Мерцающие звезды
        uint8_t s = inoise8(x * 90 + 33, y * 90 + stepTime / 6);
        if (s > 215) {
          uint8_t br = (s - 215) * 6;
          leds[XY(x, y)] = CRGB(br, br, br);
        }
      } else if (bgMode == 2) { // 🔥 Огненные угли
        uint8_t f = inoise8(x * 60, y * 80 + stepTime / 10);
        if (y >= 3) {
          leds[XY(x, y)] = ColorFromPalette(HeatColors_p, f / 4);
        }
      } else if (bgMode == 3) { // 🌀 Мягкая плазма
        uint8_t p = (sin8(x * 15 + stepTime / 12) + cos8(y * 40 + stepTime / 15)) / 2;
        leds[XY(x, y)] = CHSV(p, 200, 30);
      }
    }
  }
}

// ==========================================
// --- ОБРАБОТЧИКИ BLE NORDIC UART ---
// ==========================================

void sendBleResponse(String msg) {
  if (bleDeviceConnected && pBleTxChar != NULL) {
    pBleTxChar->setValue((uint8_t*)msg.c_str(), msg.length());
    pBleTxChar->notify();
  }
}

void processBleCommand(String cmd) {
  if (cmd == "PING") {
    sendBleResponse("PONG");
    return;
  }

  if (cmd.startsWith("AUTH:")) {
    String pass = cmd.substring(5);
    if (pass == blePassword) {
      bleAuthenticated = true;
      sendBleResponse("AUTH_OK;PASS=" + blePassword);
    } else {
      bleAuthenticated = false;
      sendBleResponse("AUTH_FAIL");
    }
    return;
  }

  if (!bleAuthenticated) {
    sendBleResponse("ERR_AUTH_REQUIRED");
    return;
  }

  if (cmd.startsWith("T:")) {
    currentText = cmd.substring(2);
    prepareText(currentText);
    scrollPos = 0;
    appMode = MODE_TEXT;
    sendBleResponse("OK_T");
  } else if (cmd.startsWith("TP:")) {
    // TP:mode,c1,c2,c3,speed,style,bg,seamless,gap
    String params = cmd.substring(3);
    int pIdx = 0;
    String tokens[10];
    int tCount = 0;
    while (tCount < 10) {
      int comma = params.indexOf(',', pIdx);
      if (comma == -1) {
        tokens[tCount++] = params.substring(pIdx);
        break;
      }
      tokens[tCount++] = params.substring(pIdx, comma);
      pIdx = comma + 1;
    }
    if (tCount >= 9) {
      textColMode = constrain(tokens[0].toInt(), 0, 9);
      color1 = parseHexColor(tokens[1]);
      color2 = parseHexColor(tokens[2]);
      color3 = parseHexColor(tokens[3]);
      scrollDelay = constrain(tokens[4].toInt(), 10, 300);
      textStyle = constrain(tokens[5].toInt(), 0, 5);
      textBgMode = constrain(tokens[6].toInt(), 0, 3);
      seamlessLoop = (tokens[7].toInt() == 1);
      loopGap = constrain(tokens[8].toInt(), 0, 30);
      appMode = MODE_TEXT;
      sendBleResponse("OK_TP");
    }
  } else if (cmd.startsWith("SETPASS:")) {
    blePassword = cmd.substring(8);
    prefs.putString("ble_pass", blePassword);
    sendBleResponse("OK_PASS;PASS=" + blePassword);
  } else if (cmd.startsWith("BRIGHT:")) {
    brightness = constrain(cmd.substring(7).toInt(), 0, 255);
    FastLED.setBrightness(brightness);
    FastLED.show();
    sendBleResponse("OK_BRIGHT");
  } else if (cmd.startsWith("EFF:")) {
    int comma = cmd.indexOf(',', 4);
    if (comma != -1) {
      currentEffect = cmd.substring(4, comma).toInt();
      effectSpeed = constrain(cmd.substring(comma + 1).toInt(), 10, 300);
    } else {
      currentEffect = cmd.substring(4).toInt();
    }
    appMode = MODE_EFFECT;
    sendBleResponse("OK_EFF");
  } else if (cmd.startsWith("PAUSE:")) {
    textRunning = (cmd.substring(6).toInt() == 1);
    sendBleResponse("OK_PAUSE");
  } else if (cmd.startsWith("LP:")) {
    int c1 = cmd.indexOf(',', 3);
    int c2 = cmd.indexOf(',', c1 + 1);
    if (c1 != -1 && c2 != -1) {
      uint8_t x = cmd.substring(3, c1).toInt();
      uint8_t y = cmd.substring(c1 + 1, c2).toInt();
      CRGB c = parseHexColor(cmd.substring(c2 + 1));
      if (x < MATRIX_W && y < MATRIX_H) {
        liveDrawBuffer[XY(x, y)] = c;
        leds[XY(x, y)] = c;
        FastLED.show();
        appMode = MODE_DRAW_LIVE;
      }
    }
    sendBleResponse("OK_LP");
  } else if (cmd.startsWith("RAW:")) {
    String hex = cmd.substring(4);
    int len = hex.length();
    for (int i = 0; i < NUM_LEDS && (i * 6 + 6) <= len; i++) {
      int x = i % MATRIX_W;
      int y = i / MATRIX_W;
      leds[XY(x, y)] = parseHexColor(hex.substring(i * 6, i * 6 + 6));
    }
    FastLED.show();
    appMode = MODE_RAW_STREAM;
  } else if (cmd == "STATUS?") {
    String st = "STATUS:{\"text\":\"" + currentText + "\",";
    st += "\"bright\":" + String(brightness) + ",";
    st += "\"speed\":" + String(scrollDelay) + ",";
    st += "\"mode\":" + String(textColMode) + ",";
    st += "\"style\":" + String(textStyle) + ",";
    st += "\"bg\":" + String(textBgMode) + ",";
    st += "\"c1\":\"" + colorToHex(color1) + "\",";
    st += "\"c2\":\"" + colorToHex(color2) + "\",";
    st += "\"c3\":\"" + colorToHex(color3) + "\",";
    st += "\"seamless\":" + String(seamlessLoop ? 1 : 0) + ",";
    st += "\"gap\":" + String(loopGap) + "}";
    sendBleResponse(st);
  }
}

void handleBlePacket(String rxVal) {
  bleRxBuffer += rxVal;
  bleRxBuffer.replace("\r", "\n");
  int nlIdx;
  while ((nlIdx = bleRxBuffer.indexOf('\n')) != -1) {
    String line = bleRxBuffer.substring(0, nlIdx);
    bleRxBuffer = bleRxBuffer.substring(nlIdx + 1);
    line.trim();
    if (line.length() > 0) {
      processBleCommand(line);
    }
  }
  if (!bleAuthenticated && bleRxBuffer.startsWith("AUTH:") && bleRxBuffer.length() >= 9) {
    processBleCommand(bleRxBuffer);
    bleRxBuffer = "";
  }
  if (bleRxBuffer.startsWith("RAW:") && bleRxBuffer.length() >= 604) {
    processBleCommand(bleRxBuffer.substring(0, 604));
    bleRxBuffer = bleRxBuffer.substring(604);
  }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleDeviceConnected = true;
    bleAuthenticated = false;
    Serial.println("[BLE] Device connected!");
  }
  void onDisconnect(BLEServer* pServer) {
    bleDeviceConnected = false;
    bleAuthenticated = false;
    Serial.println("[BLE] Device disconnected!");
  }
  void onConnParamsUpdate(uint16_t conn_handle, uint16_t interval, uint16_t latency, uint16_t timeout, uint8_t status) {
    Serial.printf("[BLE] ConnParamsUpdate: itvl=%u lat=%u to=%u status=%u\n", interval, latency, timeout, status);
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      handleBlePacket(rxValue);
    }
  }
};

// ==========================================
// --- АНИМАЦИИ И СПЕЦЭФФЕКТЫ ---
// ==========================================

void renderNyanCat(uint32_t step) {
  FastLED.clear();
  int bob = (step / 2) % 2;

  const CRGB rainbowColors[5] = {
    CRGB(255, 0, 0),     // Red
    CRGB(255, 120, 0),   // Orange
    CRGB(255, 230, 0),   // Yellow
    CRGB(0, 255, 0),     // Green
    CRGB(0, 110, 255)    // Blue
  };

  for (int x = 0; x < 13; x++) {
    int wave = ((x / 2 + step) % 2) - bob;
    for (int y = 0; y < 5; y++) {
      int sy = y - wave;
      if (sy >= 0 && sy < 5) {
        leds[XY(x, y)] = rainbowColors[sy];
      }
    }
  }

  if ((step + 3) % 4 == 0) leds[XY(2, (step % 5))] = CRGB::White;
  if ((step + 1) % 5 == 0) leds[XY(8, ((step + 2) % 5))] = CRGB::White;

  CRGB crustColor = CRGB(220, 170, 110);
  CRGB frostColor = CRGB(255, 105, 180);
  for (int cx = 13; cx <= 16; cx++) {
    for (int cy = 1; cy <= 3; cy++) {
      int y = cy + bob - 1;
      if (y >= 0 && y < 5) {
        if (cx == 13 || cx == 16 || cy == 1 || cy == 3) {
          leds[XY(cx, y)] = crustColor;
        } else {
          if ((cx + cy + step) % 3 == 0) leds[XY(cx, y)] = CRGB(255, 0, 40);
          else leds[XY(cx, y)] = frostColor;
        }
      }
    }
  }

  CRGB greyCat = CRGB(150, 150, 160);
  CRGB pinkCheek = CRGB(255, 60, 160);
  for (int cx = 17; cx <= 19; cx++) {
    for (int cy = 1; cy <= 3; cy++) {
      int y = cy + bob - 1;
      if (y >= 0 && y < 5) {
        if (cx == 17 && cy == 2) leds[XY(cx, y)] = pinkCheek;
        else if (cx == 19 && cy == 2) leds[XY(cx, y)] = CRGB(10, 10, 10);
        else leds[XY(cx, y)] = greyCat;
      }
    }
  }
  int earY = bob;
  if (earY >= 0 && earY < 5) {
    leds[XY(17, earY)] = greyCat;
    leds[XY(19, earY)] = greyCat;
  }
  int footY = 4;
  if (bob == 0) {
    leds[XY(13, footY)] = greyCat;
    leds[XY(16, footY)] = greyCat;
  }
}

void renderMatrix(uint32_t step) {
  static uint8_t dropY[MATRIX_W] = {0};
  static uint8_t dropSpeed[MATRIX_W] = {0};

  if (step % 2 == 0) {
    for (int x = 0; x < MATRIX_W; x++) {
      if (dropSpeed[x] == 0) {
        if (random8(10) == 0) {
          dropY[x] = 0;
          dropSpeed[x] = random8(1, 4);
        }
      } else {
        dropY[x]++;
        if (dropY[x] > MATRIX_H + 4) {
          dropSpeed[x] = 0;
        }
      }
    }
  }

  FastLED.clear();
  for (int x = 0; x < MATRIX_W; x++) {
    if (dropSpeed[x] > 0) {
      int head = dropY[x];
      for (int y = 0; y < MATRIX_H; y++) {
        if (y == head) {
          leds[XY(x, y)] = CRGB(220, 255, 220);
        } else if (y == head - 1) {
          leds[XY(x, y)] = CRGB(0, 255, 50);
        } else if (y == head - 2) {
          leds[XY(x, y)] = CRGB(0, 140, 20);
        } else if (y == head - 3) {
          leds[XY(x, y)] = CRGB(0, 50, 10);
        }
      }
    }
  }
}

void renderFire(uint32_t step) {
  for (int x = 0; x < MATRIX_W; x++) {
    for (int y = 0; y < MATRIX_H; y++) {
      uint8_t heat = inoise8(x * 60, (4 - y) * 80 + step * 10, step * 5);
      heat = scale8(heat, 240 - y * 40);
      leds[XY(x, y)] = ColorFromPalette(HeatColors_p, heat);
    }
  }
}

void renderEqualizer(uint32_t step) {
  FastLED.clear();
  for (int x = 0; x < MATRIX_W; x++) {
    uint8_t n = inoise8(x * 40 + step * 8, step * 4);
    int h = map(n, 40, 215, 0, 5);
    h = constrain(h, 0, 5);

    for (int y = 0; y < MATRIX_H; y++) {
      int fromBottom = 4 - y;
      if (fromBottom < h) {
        CRGB col;
        if (fromBottom <= 1) col = CRGB(0, 255, 30);
        else if (fromBottom <= 3) col = CRGB(255, 200, 0);
        else col = CRGB(255, 20, 20);
        leds[XY(x, y)] = col;
      }
    }
  }
}

void renderPacman(uint32_t step) {
  FastLED.clear();
  int pacX = (step / 2) % (MATRIX_W + 12) - 6;
  int ghostX = pacX - 7;
  bool mouthOpen = (step % 4) < 2;

  for (int x = 0; x < MATRIX_W; x += 3) {
    if (x > pacX) {
      leds[XY(x, 2)] = CRGB(255, 200, 120);
    }
  }

  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      int px = pacX + dx;
      int py = 2 + dy;
      if (px >= 0 && px < MATRIX_W && py >= 0 && py < MATRIX_H) {
        if (mouthOpen && dx == 1 && dy == 0) continue;
        leds[XY(px, py)] = CRGB(255, 220, 0);
      }
    }
  }

  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      int gx = ghostX + dx;
      int gy = 2 + dy;
      if (gx >= 0 && gx < MATRIX_W && gy >= 0 && gy < MATRIX_H) {
        if (dy == 1 && dx == 0) continue;
        leds[XY(gx, gy)] = CRGB(255, 20, 40);
      }
    }
  }
}

void renderHeart(uint32_t step) {
  FastLED.clear();
  uint8_t phase = step % 32;
  float scale = 0.6;
  if (phase < 4) scale = 1.0;
  else if (phase < 8) scale = 0.7;
  else if (phase < 12) scale = 1.2;
  else if (phase < 16) scale = 0.6;

  static const uint8_t heartMap[5] = {
    0b01010,
    0b11111,
    0b11111,
    0b01110,
    0b00100
  };

  int centerX = 10;
  for (int y = 0; y < 5; y++) {
    for (int bx = 0; bx < 5; bx++) {
      if ((heartMap[y] >> (4 - bx)) & 1) {
        int x = centerX - 2 + bx;
        CRGB col = CRGB(255 * scale, 0, 50 * scale);
        leds[XY(x, y)] = col;
      }
    }
  }

  int pulseX = (step * 2) % MATRIX_W;
  leds[XY(pulseX, 2)] = CRGB(0, 255, 200);
}

void renderPlasma(uint32_t step) {
  for (int x = 0; x < MATRIX_W; x++) {
    for (int y = 0; y < MATRIX_H; y++) {
      uint8_t p1 = sin8(x * 18 + step * 4);
      uint8_t p2 = cos8(y * 45 + step * 6);
      uint8_t p3 = sin8((x + y) * 20 + step * 3);
      uint8_t hue = (p1 + p2 + p3) / 3;
      leds[XY(x, y)] = CHSV(hue, 240, 255);
    }
  }
}

// ==========================================
// --- HTML / CSS / JS ВЕБ-ПРИЛОЖЕНИЕ ---
// ==========================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>ESP32 LED Matrix Studio</title>
<style>
  :root {
    --bg: #090d16;
    --card: #131a2b;
    --card-hover: #19233a;
    --accent: #00f0ff;
    --accent-grad: linear-gradient(135deg, #00f0ff, #ff007b);
    --text: #f0f4fc;
    --text-dim: #8ba0c0;
    --border: #202b44;
    --led-off: #172033;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; -webkit-tap-highlight-color: transparent; }
  body { background: var(--bg); color: var(--text); min-height: 100vh; padding: 12px; display: flex; justify-content: center; }
  .container { width: 100%; max-width: 560px; display: flex; flex-direction: column; gap: 14px; }

  /* Navigation Tabs */
  .nav-tabs { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; background: #0e1424; padding: 6px; border-radius: 14px; border: 1px solid var(--border); }
  .nav-btn { background: transparent; border: none; color: var(--text-dim); font-size: 12px; font-weight: 600; padding: 10px 4px; border-radius: 10px; cursor: pointer; text-align: center; transition: all 0.2s; }
  .nav-btn.active { background: var(--card); color: var(--accent); box-shadow: 0 4px 12px rgba(0,0,0,0.4); border: 1px solid var(--border); }

  .tab-content { display: none; flex-direction: column; gap: 14px; }
  .tab-content.active { display: flex; }

  .card { background: var(--card); border: 1px solid var(--border); border-radius: 16px; padding: 16px; display: flex; flex-direction: column; gap: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.3); }
  .card-title { font-size: 13px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.8px; color: var(--text-dim); display: flex; justify-content: space-between; align-items: center; }

  /* Buttons & Inputs */
  .input-row { display: flex; gap: 8px; }
  input[type="text"] { flex: 1; background: #0a101d; border: 1px solid var(--border); border-radius: 10px; color: #fff; font-size: 15px; padding: 10px 14px; outline: none; }
  input[type="text"]:focus { border-color: var(--accent); }
  button { background: var(--accent-grad); color: #fff; border: none; font-size: 13px; font-weight: 600; padding: 10px 16px; border-radius: 10px; cursor: pointer; transition: transform 0.1s, opacity 0.2s; }
  button:active { transform: scale(0.97); }
  button.sec { background: #1a233a; color: var(--text); border: 1px solid var(--border); }
  button.danger { background: #ff2d55; color: #fff; }

  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; }
  .grid-4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; }

  /* Sliders */
  .slider-group { display: flex; flex-direction: column; gap: 6px; }
  .slider-head { display: flex; justify-content: space-between; font-size: 13px; }
  .slider-val { font-weight: 600; color: var(--accent); }
  input[type="range"] { -webkit-appearance: none; width: 100%; height: 6px; border-radius: 4px; background: #202b44; outline: none; }
  input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: var(--accent); cursor: pointer; box-shadow: 0 0 6px rgba(0,240,255,0.6); }

  /* Presets */
  .presets { display: flex; flex-wrap: wrap; gap: 6px; }
  .preset-btn { background: #182238; border: 1px solid var(--border); color: #c4d4ee; font-size: 12px; padding: 6px 10px; border-radius: 8px; cursor: pointer; transition: all 0.2s; }
  .preset-btn:hover { border-color: var(--accent); background: #22304e; }

  /* Mode Button */
  .btn-select { background: #151e33; border: 1px solid var(--border); color: var(--text); padding: 10px 6px; border-radius: 10px; font-size: 12px; font-weight: 600; text-align: center; cursor: pointer; transition: all 0.2s; }
  .btn-select.active { border-color: var(--accent); background: rgba(0,240,255,0.12); color: var(--accent); box-shadow: 0 0 10px rgba(0,240,255,0.2); }

  /* Canvas 20x5 */
  .matrix-canvas-wrap { display: flex; justify-content: center; padding: 12px; background: #070b14; border-radius: 14px; border: 1px solid var(--border); touch-action: none; }
  .pixel-matrix { display: grid; grid-template-columns: repeat(20, 1fr); gap: 4px; width: 100%; max-width: 500px; }
  .led-pixel { aspect-ratio: 1; border-radius: 50%; background: var(--led-off); cursor: pointer; transition: background 0.05s; border: 1px solid rgba(255,255,255,0.04); }

  /* Color Palette */
  .palette-bar { display: flex; gap: 6px; align-items: center; overflow-x: auto; padding-bottom: 4px; }
  .pal-color { width: 32px; height: 32px; border-radius: 50%; cursor: pointer; border: 2px solid transparent; flex-shrink: 0; }
  .pal-color.active { border-color: #fff; transform: scale(1.1); box-shadow: 0 0 8px rgba(255,255,255,0.5); }
  input[type="color"] { -webkit-appearance: none; border: none; width: 36px; height: 36px; border-radius: 50%; cursor: pointer; background: transparent; flex-shrink: 0; }
  input[type="color"]::-webkit-color-swatch-wrapper { padding: 0; }
  input[type="color"]::-webkit-color-swatch { border: 2px solid var(--border); border-radius: 50%; }

  /* Timeline */
  .timeline { display: flex; gap: 8px; overflow-x: auto; padding: 6px 0; align-items: center; }
  .frame-thumb { background: #172138; border: 2px solid var(--border); border-radius: 8px; min-width: 54px; height: 36px; display: flex; align-items: center; justify-content: center; font-size: 12px; font-weight: 700; cursor: pointer; color: var(--text-dim); }
  .frame-thumb.active { border-color: var(--accent); color: var(--accent); background: rgba(0,240,255,0.15); }

  /* Effects cards */
  .effect-card { background: #151f36; border: 1px solid var(--border); border-radius: 14px; padding: 14px; display: flex; flex-direction: column; gap: 10px; transition: all 0.2s; }
  .effect-card:hover { border-color: #31436b; }
  .effect-card.active { border-color: var(--accent); background: rgba(0,240,255,0.08); box-shadow: 0 0 16px rgba(0,240,255,0.15); }
  .eff-head { display: flex; align-items: center; justify-content: space-between; }
  .eff-info h4 { font-size: 15px; font-weight: 700; }
  .eff-info p { font-size: 12px; color: var(--text-dim); margin-top: 2px; }
  .eff-actions { display: flex; gap: 8px; }
  .eff-actions button { flex: 1; padding: 8px 12px; font-size: 12px; }

  #toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #1c2742; border: 1px solid var(--accent); color: #fff; padding: 10px 22px; border-radius: 30px; font-size: 13px; font-weight: 600; opacity: 0; pointer-events: none; transition: opacity 0.3s; box-shadow: 0 8px 24px rgba(0,0,0,0.6); z-index: 100; }
  #toast.show { opacity: 1; }
</style>
</head>
<body>

<div class="container">
  <!-- Навигация по табам -->
  <div class="nav-tabs">
    <button class="nav-btn active" id="btn-tabText" onclick="switchTab('tabText')">🔤 Текст</button>
    <button class="nav-btn" id="btn-tabDraw" onclick="switchTab('tabDraw')">🎨 Рисование</button>
    <button class="nav-btn" id="btn-tabEffects" onclick="switchTab('tabEffects')">🎬 Эффекты</button>
    <button class="nav-btn" id="btn-tabSettings" onclick="switchTab('tabSettings')">⚙️ Инфо</button>
  </div>

  <!-- ТАБ 1: ТЕКСТ -->
  <div id="tabText" class="tab-content active">
    <div class="card">
      <div class="card-title">Бегущая строка</div>
      <div class="input-row">
        <input type="text" id="textInput" maxlength="120" placeholder="Введите текст (русский / eng)...">
        <button onclick="applyText()">Пуск</button>
      </div>
      <div class="card-title">Быстрые фразы</div>
      <div class="presets">
        <span class="preset-btn" onclick="setPresetDash()">--- Линия без пауз</span>
        <span class="preset-btn" onclick="setPreset('>>> >>> >>>')">&gt;&gt;&gt; Стрелы</span>
        <span class="preset-btn" onclick="setPreset('ЖИЗНЬ ПРЕКРАСНА 🙂')">ЖИЗНЬ ПРЕКРАСНА 🙂</span>
        <span class="preset-btn" onclick="setPreset('МИР И ЛЮБОВЬ ❤️')">МИР И ЛЮБОВЬ ❤️</span>
        <span class="preset-btn" onclick="setPreset('$100 / ДЕНЬ')">$100 / ДЕНЬ</span>
        <span class="preset-btn" onclick="setPreset('ВХОД ТУТ →')">ВХОД ТУТ →</span>
        <span class="preset-btn" onclick="setPreset('STATUS: OK ✅')">STATUS: OK ✅</span>
        <span class="preset-btn" onclick="setPreset('НОЧЬ 🌙 ТИШИНА')">НОЧЬ 🌙 ТИШИНА</span>
      </div>
      <div class="card-title" style="margin-top:6px;">Спецсимволы и стрелки (5x5)</div>
      <div class="emoji-picker" style="display:flex; flex-wrap:wrap; gap:6px; padding:2px;">
        <span class="preset-btn" onclick="insertEmoji('🙂')">🙂 Улыбка</span>
        <span class="preset-btn" onclick="insertEmoji('❤️')">❤️ Сердце</span>
        <span class="preset-btn" onclick="insertEmoji('🌙')">🌙 Луна</span>
        <span class="preset-btn" onclick="insertEmoji('✅')">✅ Галочка</span>
        <span class="preset-btn" onclick="insertEmoji('❌')">❌ Крестик</span>
        <span class="preset-btn" onclick="insertEmoji('←')">← Влево</span>
        <span class="preset-btn" onclick="insertEmoji('→')">→ Вправо</span>
        <span class="preset-btn" onclick="insertEmoji('↑')">↑ Вверх</span>
        <span class="preset-btn" onclick="insertEmoji('↓')">↓ Вниз</span>
        <span class="preset-btn" onclick="insertEmoji('↔')">↔ Влево-вправо</span>
        <span class="preset-btn" onclick="insertEmoji('«')">« Кавычки</span>
        <span class="preset-btn" onclick="insertEmoji('»')">»</span>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Режим зацикливания</div>
      <div class="grid-2">
        <div class="btn-select active" id="loopModeSeamless" onclick="setSeamless(true)">🔄 Бесшовно (сразу)</div>
        <div class="btn-select" id="loopModeClassic" onclick="setSeamless(false)">⏹ С паузой (в край)</div>
      </div>
      <div class="slider-group" id="loopGapGroup">
        <div class="slider-head"><span>Промежуток между повторами</span><span class="slider-val" id="loopGapVal">3 пикс</span></div>
        <input type="range" id="loopGapSlider" min="0" max="20" value="3" oninput="updateLoopGap(this.value)">
      </div>
    </div>

    <div class="card">
      <div class="card-title">Цвет текста (10 палитр)</div>
      <div style="display:grid; grid-template-columns:repeat(5, 1fr); gap:6px;">
        <div class="btn-select" id="tm0" onclick="setTextMode(0)">🌈 Радуга</div>
        <div class="btn-select" id="tm1" onclick="setTextMode(1)">🎨 Моно</div>
        <div class="btn-select" id="tm2" onclick="setTextMode(2)">🌌 2X Град</div>
        <div class="btn-select" id="tm3" onclick="setTextMode(3)">🌅 3X Град</div>
        <div class="btn-select" id="tm4" onclick="setTextMode(4)">🔥 Огонь</div>
        <div class="btn-select" id="tm5" onclick="setTextMode(5)">⚡ Неон</div>
        <div class="btn-select" id="tm6" onclick="setTextMode(6)">🟢 Матрица</div>
        <div class="btn-select" id="tm7" onclick="setTextMode(7)">🔮 Плазма</div>
        <div class="btn-select" id="tm8" onclick="setTextMode(8)">✨ Звезды</div>
        <div class="btn-select" id="tm9" onclick="setTextMode(9)">🍬 Пастель</div>
      </div>
      <div id="textColorPickers" style="display:none; gap:10px; align-items:center; flex-wrap:wrap; margin-top:4px;">
        <span>Цвет 1: <input type="color" id="tc1" value="#00ffff" onchange="sendTextConfig()"></span>
        <span id="tc2Wrap" style="display:none;">Цвет 2: <input type="color" id="tc2" value="#ff0080" onchange="sendTextConfig()"></span>
        <span id="tc3Wrap" style="display:none;">Цвет 3: <input type="color" id="tc3" value="#ffe600" onchange="sendTextConfig()"></span>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Стиль движения текста</div>
      <div class="grid-3">
        <div class="btn-select active" id="ts0" onclick="setTextStyle(0)">➡️ Скролл</div>
        <div class="btn-select" id="ts1" onclick="setTextStyle(1)">⌨️ Машинка</div>
        <div class="btn-select" id="ts2" onclick="setTextStyle(2)">🌊 Волна Y</div>
        <div class="btn-select" id="ts3" onclick="setTextStyle(3)">💓 Дыхание</div>
        <div class="btn-select" id="ts4" onclick="setTextStyle(4)">⚡ Глитч</div>
        <div class="btn-select" id="ts5" onclick="setTextStyle(5)">🎯 Центр</div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Фоновый спецэффект под текстом</div>
      <div class="grid-4">
        <div class="btn-select active" id="tbg0" onclick="setTextBg(0)">⬛ Черный</div>
        <div class="btn-select" id="tbg1" onclick="setTextBg(1)">🌌 Звезды</div>
        <div class="btn-select" id="tbg2" onclick="setTextBg(2)">🔥 Угли</div>
        <div class="btn-select" id="tbg3" onclick="setTextBg(3)">🌀 Плазма</div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Скорость текста</div>
      <div class="slider-group">
        <div class="slider-head"><span>Задержка шага</span><span class="slider-val" id="textSpeedVal">65 мс</span></div>
        <input type="range" id="textSpeed" min="15" max="200" value="65" oninput="updateTextSpeed(this.value)">
      </div>
      <div class="grid-2">
        <button class="sec" id="textPauseBtn" onclick="toggleTextPause()">⏸ Пауза</button>
        <button onclick="saveTextSettings()">💾 Сохранить</button>
      </div>
    </div>
  </div>

  <!-- ТАБ 2: РИСОВАНИЕ И АНИМАЦИЯ -->
  <div id="tabDraw" class="tab-content">
    <!-- Блок быстрой загрузки шаблонов для редактирования -->
    <div class="card">
      <div class="card-title">Готовые шаблоны для редактирования</div>
      <div class="presets">
        <span class="preset-btn" onclick="loadTemplate('nyan')">🐱 Нян-Кэт (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('pacman')">🟡 Пакман (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('heart')">💓 Сердце (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('invader')">👾 Пришелец (4 кадра)</span>
        <span class="preset-btn" onclick="loadTemplate('car')">🏎️ Спорткар (4 кадра)</span>
      </div>
    </div>

    <div class="card">
      <div class="card-title">
        <span>Холст 20×5</span>
        <span id="curFrameLabel" style="color:var(--accent);">Кадр 1/1</span>
      </div>
      <!-- Матрица для рисования -->
      <div class="matrix-canvas-wrap" id="canvasWrap">
        <div class="pixel-matrix" id="pixelGrid"></div>
      </div>

      <!-- Палитра цветов -->
      <div class="palette-bar">
        <div class="pal-color active" style="background:#ff0055" onclick="selectColor('#ff0055', this)"></div>
        <div class="pal-color" style="background:#ff5500" onclick="selectColor('#ff5500', this)"></div>
        <div class="pal-color" style="background:#ffcc00" onclick="selectColor('#ffcc00', this)"></div>
        <div class="pal-color" style="background:#00ff66" onclick="selectColor('#00ff66', this)"></div>
        <div class="pal-color" style="background:#00f0ff" onclick="selectColor('#00f0ff', this)"></div>
        <div class="pal-color" style="background:#0066ff" onclick="selectColor('#0066ff', this)"></div>
        <div class="pal-color" style="background:#aa00ff" onclick="selectColor('#aa00ff', this)"></div>
        <div class="pal-color" style="background:#ffffff" onclick="selectColor('#ffffff', this)"></div>
        <div class="pal-color" style="background:#000000; border:1px solid #444;" title="Ластик" onclick="selectColor('#000000', this)"></div>
        <input type="color" id="customPen" value="#ff0055" onchange="selectColor(this.value, null)">
      </div>

      <!-- Инструменты -->
      <div class="grid-3">
        <button class="sec" onclick="fillCanvas()">🪣 Залить</button>
        <button class="sec" onclick="clearCanvas()">🗑️ Очистить</button>
        <button class="sec" onclick="duplicateFrame()">📋 Дубликат</button>
      </div>
    </div>

    <!-- Таймлайн кадров -->
    <div class="card">
      <div class="card-title">
        <span>Кадры анимации</span>
        <button class="sec" style="padding:4px 10px; font-size:12px;" onclick="addFrame()">+ Новый кадр</button>
      </div>
      <div class="timeline" id="timelineBar"></div>

      <div class="slider-group">
        <div class="slider-head"><span>Скорость анимации</span><span class="slider-val" id="fpsVal">6 FPS</span></div>
        <input type="range" id="fpsSlider" min="1" max="25" value="6" oninput="updateFps(this.value)">
      </div>

      <div class="grid-2">
        <button id="playAnimBtn" onclick="playCustomAnim()">▶ Запустить на матрице</button>
        <button class="danger" onclick="deleteFrame()">Удалить кадр</button>
      </div>

      <div class="grid-2">
        <button class="sec" onclick="exportAnimJson()">📥 Скачать JSON</button>
        <button class="sec" onclick="importAnimJson()">📤 Загрузить JSON</button>
      </div>
    </div>
  </div>

  <!-- ТАБ 3: ВСТРОЕННЫЕ ЭФФЕКТЫ -->
  <div id="tabEffects" class="tab-content">
    <!-- Слайдер скорости воспроизведения эффектов -->
    <div class="card">
      <div class="card-title">Скорость встроенных эффектов</div>
      <div class="slider-group">
        <div class="slider-head"><span>Скорость анимации</span><span class="slider-val" id="effSpeedVal">55 мс</span></div>
        <input type="range" id="effSpeedSlider" min="15" max="200" value="55" oninput="updateEffectSpeed(this.value)">
      </div>
    </div>

    <div class="card">
      <div class="card-title">Галерея эффектов</div>

      <div class="effect-card" id="eff0">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🐱 Нян-Кэт (Nyan Cat)</h4>
            <p>Радужный шлейф волнами и летящий кот</p>
          </div>
          <span style="font-size:24px;">🌈</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffect(0)">▶ Запустить</button>
          <button class="sec" onclick="loadTemplate('nyan')">✏️ В редактор</button>
        </div>
      </div>

      <div class="effect-card" id="eff4">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🟡 Пакман (Pac-Man)</h4>
            <p>Пакман поедает точки, убегая от призрака</p>
          </div>
          <span style="font-size:24px;">👻</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffect(4)">▶ Запустить</button>
          <button class="sec" onclick="loadTemplate('pacman')">✏️ В редактор</button>
        </div>
      </div>

      <div class="effect-card" id="eff5">
        <div class="eff-head">
          <div class="eff-info">
            <h4>💓 Сердцебиение (Heartbeat)</h4>
            <p>Пульс пиксельного сердца и линия ЭКГ</p>
          </div>
          <span style="font-size:24px;">❤️</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffect(5)">▶ Запустить</button>
          <button class="sec" onclick="loadTemplate('heart')">✏️ В редактор</button>
        </div>
      </div>

      <div class="effect-card" id="eff1">
        <div class="eff-head">
          <div class="eff-info">
            <h4>💻 Матрица (Matrix Rain)</h4>
            <p>Цифровой зеленый дождь с белыми каплями</p>
          </div>
          <span style="font-size:24px;">🟩</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffect(1)">▶ Запустить</button>
        </div>
      </div>

      <div class="effect-card" id="eff2">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🔥 Огонь (Fire Flame)</h4>
            <p>Плавная физика языков пламени</p>
          </div>
          <span style="font-size:24px;">🔥</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffect(2)">▶ Запустить</button>
        </div>
      </div>

      <div class="effect-card" id="eff3">
        <div class="eff-head">
          <div class="eff-info">
            <h4>📊 Эквалайзер (VU Meter)</h4>
            <p>20 скачущих частотных полос</p>
          </div>
          <span style="font-size:24px;">🎵</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffect(3)">▶ Запустить</button>
        </div>
      </div>

      <div class="effect-card" id="eff6">
        <div class="eff-head">
          <div class="eff-info">
            <h4>🌀 Плазма (Plasma)</h4>
            <p>Психоделические 2D волны цвета</p>
          </div>
          <span style="font-size:24px;">🔮</span>
        </div>
        <div class="eff-actions">
          <button onclick="startEffect(6)">▶ Запустить</button>
        </div>
      </div>
    </div>
  </div>

  <!-- ТАБ 4: НАСТРОЙКИ И ИНФО -->
  <div id="tabSettings" class="tab-content">
    <div class="card">
      <div class="card-title">Глобальная яркость</div>
      <div class="slider-group">
        <div class="slider-head"><span>Яркость всех диодов</span><span class="slider-val" id="globalBrightVal">40</span></div>
        <input type="range" id="globalBright" min="5" max="255" value="40" oninput="updateBright(this.value)">
      </div>
      <button onclick="saveGlobalSettings()">💾 Сохранить яркость</button>
    </div>

    <div class="card">
      <div class="card-title">Bluetooth LE Управление</div>
      <p style="font-size:13px; line-height:1.6; color:var(--text-dim);">
        • Имя BLE: <b style="color:var(--accent);">ESP32-Matrix-BLE</b><br>
        • PIN для подключения: <b id="blePassDisplay" style="color:#00ff66;">1234</b><br>
        • Сервис: <b>Nordic UART (NUS)</b>
      </p>
      <a href="/ble" target="_blank" style="text-decoration:none;"><button style="width:100%; margin-top:4px;">📱 Открыть Web Bluetooth Studio</button></a>
    </div>

    <div class="card">
      <div class="card-title">О системе</div>
      <p style="font-size:13px; line-height:1.6; color:var(--text-dim);">
        • Контроллер: <b>ESP32-C3 Super Mini (RISC-V)</b><br>
        • Матрица: <b>20×5 (100 WS2812B LEDs, Pin 8)</b><br>
        • Wi-Fi AP: <b>ESP32-Matrix (192.168.4.1)</b><br>
        • Быстрый адрес: <b>http://matrix.local</b>
      </p>
      <button class="danger" style="margin-top:6px;" onclick="rebootEsp()">Перезагрузить ESP32</button>
    </div>
  </div>

</div>

<div id="toast">Готово</div>

<script>
  let activeColor = '#ff0055';
  let isDrawing = false;
  let curFrameIdx = 0;
  let animFrames = [ Array(100).fill('#000000') ];
  let fps = 6;
  let effectSpeedMs = 55;

  function showToast(msg) {
    const t = document.getElementById('toast');
    t.innerText = msg;
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), 2000);
  }

  function switchTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    document.getElementById('btn-' + tabId).classList.add('active');

    if (tabId === 'tabDraw') {
      sendLiveFrame();
    }
  }

  // --- ИНИЦИАЛИЗАЦИЯ ХОЛСТА 20x5 ---
  const grid = document.getElementById('pixelGrid');
  for (let i = 0; i < 100; i++) {
    const p = document.createElement('div');
    p.className = 'led-pixel';
    p.dataset.idx = i;
    p.addEventListener('pointerdown', (e) => {
      isDrawing = true;
      paintPixel(i);
      e.preventDefault();
    });
    p.addEventListener('pointerenter', () => {
      if (isDrawing) paintPixel(i);
    });
    grid.appendChild(p);
  }

  window.addEventListener('pointerup', () => { isDrawing = false; });
  window.addEventListener('pointercancel', () => { isDrawing = false; });

  function selectColor(c, el) {
    activeColor = c;
    document.querySelectorAll('.pal-color').forEach(p => p.classList.remove('active'));
    if (el) el.classList.add('active');
    document.getElementById('customPen').value = c;
  }

  function paintPixel(idx) {
    animFrames[curFrameIdx][idx] = activeColor;
    updatePixelDOM(idx, activeColor);
    let x = idx % 20;
    let y = Math.floor(idx / 20);
    fetch('/api/livepixel?x=' + x + '&y=' + y + '&c=' + activeColor.substring(1)).catch(()=>{});
  }

  function updatePixelDOM(idx, color) {
    const p = grid.children[idx];
    if (color === '#000000' || color === '#000') {
      p.style.background = 'var(--led-off)';
      p.style.boxShadow = 'none';
    } else {
      p.style.background = color;
      p.style.boxShadow = '0 0 6px ' + color;
    }
  }

  function renderCurrentFrame() {
    const frame = animFrames[curFrameIdx];
    for (let i = 0; i < 100; i++) {
      updatePixelDOM(i, frame[i]);
    }
    document.getElementById('curFrameLabel').innerText = `Кадр ${curFrameIdx + 1}/${animFrames.length}`;
    renderTimeline();
  }

  function fillCanvas() {
    animFrames[curFrameIdx].fill(activeColor);
    renderCurrentFrame();
    sendLiveFrame();
  }

  function clearCanvas() {
    animFrames[curFrameIdx].fill('#000000');
    renderCurrentFrame();
    sendLiveFrame();
  }

  function addFrame() {
    if (animFrames.length >= 16) {
      showToast("Максимум 16 кадров");
      return;
    }
    animFrames.push(Array(100).fill('#000000'));
    curFrameIdx = animFrames.length - 1;
    renderCurrentFrame();
    sendLiveFrame();
    showToast("Добавлен кадр " + (curFrameIdx + 1));
  }

  function duplicateFrame() {
    if (animFrames.length >= 16) {
      showToast("Максимум 16 кадров");
      return;
    }
    animFrames.push([...animFrames[curFrameIdx]]);
    curFrameIdx = animFrames.length - 1;
    renderCurrentFrame();
    sendLiveFrame();
    showToast("Кадр скопирован");
  }

  function deleteFrame() {
    if (animFrames.length <= 1) {
      clearCanvas();
      return;
    }
    animFrames.splice(curFrameIdx, 1);
    if (curFrameIdx >= animFrames.length) curFrameIdx = animFrames.length - 1;
    renderCurrentFrame();
    sendLiveFrame();
    showToast("Кадр удален");
  }

  function renderTimeline() {
    const bar = document.getElementById('timelineBar');
    bar.innerHTML = '';
    animFrames.forEach((_, idx) => {
      const btn = document.createElement('div');
      btn.className = 'frame-thumb' + (idx === curFrameIdx ? ' active' : '');
      btn.innerText = '#' + (idx + 1);
      btn.onclick = () => {
        curFrameIdx = idx;
        renderCurrentFrame();
        sendLiveFrame();
      };
      bar.appendChild(btn);
    });
  }

  function updateFps(v) {
    fps = parseInt(v);
    document.getElementById('fpsVal').innerText = v + ' FPS';
  }

  function sendLiveFrame() {
    const frame = animFrames[curFrameIdx];
    let hex = '';
    for (let c of frame) {
      hex += (c.startsWith('#') ? c.substring(1) : c).padStart(6, '0');
    }
    fetch('/api/liveframe', { method: 'POST', body: hex }).catch(()=>{});
  }

  function playCustomAnim() {
    let payload = {
      fps: fps,
      count: animFrames.length,
      frames: animFrames
    };
    fetch('/api/anim', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    }).then(() => showToast("Анимация запущена на матрице!"))
      .catch(e => console.log(e));
  }

  function exportAnimJson() {
    const data = JSON.stringify({ fps, frames: animFrames }, null, 2);
    const blob = new Blob([data], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'matrix_animation.json';
    a.click();
    showToast("JSON экспортирован");
  }

  function importAnimJson() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = (e) => {
      const file = e.target.files[0];
      const reader = new FileReader();
      reader.onload = (ev) => {
        try {
          const parsed = JSON.parse(ev.target.result);
          if (parsed.frames && Array.isArray(parsed.frames)) {
            animFrames = parsed.frames;
            if (parsed.fps) {
              fps = parsed.fps;
              document.getElementById('fpsSlider').value = fps;
              document.getElementById('fpsVal').innerText = fps + ' FPS';
            }
            curFrameIdx = 0;
            renderCurrentFrame();
            showToast("Анимация загружена!");
          }
        } catch (err) {
          showToast("Ошибка в JSON файле");
        }
      };
      reader.readAsText(file);
    };
    input.click();
  }

  // --- ГЕНЕРАТОРЫ ШАБЛОНОВ ДЛЯ РЕДАКТИРОВАНИЯ ---
  function loadTemplate(type) {
    let newFrames = [];
    if (type === 'nyan') {
      const rainbow = ['#ff0000', '#ff7700', '#ffee00', '#00ff00', '#0077ff'];
      const crust = '#dca064';
      const frost = '#ff66b2';
      const cat = '#9696a0';
      const cheek = '#ff3399';
      const eye = '#111111';

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let bob = step % 2;

        for (let x = 0; x < 13; x++) {
          let wave = ((Math.floor(x / 2) + step) % 2) - bob;
          for (let y = 0; y < 5; y++) {
            let sy = y - wave;
            if (sy >= 0 && sy < 5) frame[y * 20 + x] = rainbow[sy];
          }
        }
        if ((step + 1) % 2 === 0) frame[1 * 20 + 2] = '#ffffff';
        if ((step + 2) % 2 === 0) frame[3 * 20 + 8] = '#ffffff';

        for (let cx = 13; cx <= 16; cx++) {
          for (let cy = 1; cy <= 3; cy++) {
            let y = cy + bob - 1;
            if (y >= 0 && y < 5) {
              let idx = y * 20 + cx;
              frame[idx] = (cx === 13 || cx === 16 || cy === 1 || cy === 3) ? crust : (((cx + cy + step) % 2 === 0) ? '#ff0044' : frost);
            }
          }
        }
        for (let cx = 17; cx <= 19; cx++) {
          for (let cy = 1; cy <= 3; cy++) {
            let y = cy + bob - 1;
            if (y >= 0 && y < 5) {
              let idx = y * 20 + cx;
              if (cx === 17 && cy === 2) frame[idx] = cheek;
              else if (cx === 19 && cy === 2) frame[idx] = eye;
              else frame[idx] = cat;
            }
          }
        }
        let earY = bob;
        if (earY >= 0 && earY < 5) {
          frame[earY * 20 + 17] = cat;
          frame[earY * 20 + 19] = cat;
        }
        if (bob === 0) {
          frame[4 * 20 + 13] = cat;
          frame[4 * 20 + 16] = cat;
        }
        newFrames.push(frame);
      }
    } else if (type === 'pacman') {
      const yellow = '#ffdd00';
      const red = '#ff1133';
      const dot = '#ffcc99';

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let pacX = 4 + step * 3;
        let ghostX = pacX - 7;
        let mouthOpen = (step % 2 === 0);

        for (let x = 0; x < 20; x += 3) {
          if (x > pacX) frame[2 * 20 + x] = dot;
        }
        for (let dx = -1; dx <= 1; dx++) {
          for (let dy = -1; dy <= 1; dy++) {
            let px = pacX + dx;
            let py = 2 + dy;
            if (px >= 0 && px < 20 && py >= 0 && py < 5) {
              if (mouthOpen && dx === 1 && dy === 0) continue;
              frame[py * 20 + px] = yellow;
            }
          }
        }
        for (let dx = -1; dx <= 1; dx++) {
          for (let dy = -1; dy <= 1; dy++) {
            let gx = ghostX + dx;
            let gy = 2 + dy;
            if (gx >= 0 && gx < 20 && gy >= 0 && gy < 5) {
              if (dy === 1 && dx === 0) continue;
              frame[gy * 20 + gx] = red;
            }
          }
        }
        newFrames.push(frame);
      }
    } else if (type === 'heart') {
      const red = '#ff0044';
      const pink = '#ff4488';
      const cyan = '#00ffcc';

      const mapBig = [ 0b01010, 0b11111, 0b11111, 0b01110, 0b00100 ];
      const mapSmall = [ 0b00000, 0b01010, 0b01110, 0b00100, 0b00000 ];

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let isBig = (step === 1 || step === 2);
        let m = isBig ? mapBig : mapSmall;
        let col = isBig ? pink : red;

        for (let y = 0; y < 5; y++) {
          for (let bx = 0; bx < 5; bx++) {
            if ((m[y] >> (4 - bx)) & 1) {
              frame[y * 20 + 8 + bx] = col;
            }
          }
        }
        frame[2 * 20 + ((step * 5) % 20)] = cyan;
        newFrames.push(frame);
      }
    } else if (type === 'invader') {
      const green = '#00ff66';
      const lime = '#88ff00';
      const m1 = [0b0010100, 0b0101010, 0b1111111, 0b1010101, 0b1000001];
      const m2 = [0b0010100, 0b0101010, 0b1111111, 0b0010100, 0b0100010];

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        let shiftX = (step < 2) ? 4 : 9;
        let m = (step % 2 === 0) ? m1 : m2;
        let col = (step % 2 === 0) ? green : lime;

        for (let y = 0; y < 5; y++) {
          for (let bx = 0; bx < 7; bx++) {
            if ((m[y] >> (6 - bx)) & 1) {
              frame[y * 20 + shiftX + bx] = col;
            }
          }
        }
        newFrames.push(frame);
      }
    } else if (type === 'car') {
      const red = '#ff1100';
      const cyan = '#00f0ff';
      const yellow = '#ffea00';
      const grey = '#445566';

      for (let step = 0; step < 4; step++) {
        let frame = Array(100).fill('#000000');
        for (let x = 0; x < 20; x += 4) {
          let rx = (x + step) % 20;
          frame[4 * 20 + rx] = '#555555';
        }
        // Машина в центре x: 7..14
        frame[1 * 20 + 9] = cyan; frame[1 * 20 + 10] = cyan; frame[1 * 20 + 11] = cyan;
        for (let x = 7; x <= 14; x++) frame[2 * 20 + x] = red;
        frame[2 * 20 + 14] = yellow; // фара
        frame[3 * 20 + 8] = grey; frame[3 * 20 + 13] = grey; // колеса
        newFrames.push(frame);
      }
    }

    if (newFrames.length > 0) {
      animFrames = newFrames;
      curFrameIdx = 0;
      switchTab('tabDraw');
      renderCurrentFrame();
      sendLiveFrame();
      showToast("Шаблон загружен в редактор! Можно дорисовывать");
    }
  }

  // --- ВСТРОЕННЫЕ ЭФФЕКТЫ ---
  function startEffect(id) {
    document.querySelectorAll('.effect-card').forEach((c, i) => {
      c.classList.toggle('active', i === id);
    });
    fetch('/api/effect?id=' + id + '&spd=' + effectSpeedMs)
      .then(() => showToast("Эффект запущен!"))
      .catch(()=>{});
  }

  let effSpeedTimer = null;
  function updateEffectSpeed(v) {
    effectSpeedMs = parseInt(v);
    document.getElementById('effSpeedVal').innerText = v + ' мс';
    clearTimeout(effSpeedTimer);
    effSpeedTimer = setTimeout(() => {
      fetch('/api/effect_speed?spd=' + effectSpeedMs).catch(()=>{});
    }, 70);
  }

  // --- ТЕКСТОВАЯ СТРОКА ---
  let textState = { mode: 0, speed: 65, running: true };

  function applyText() {
    let txt = document.getElementById('textInput').value.trim();
    if (!txt) txt = " ";
    fetch('/api/text?set=' + encodeURIComponent(txt))
      .then(() => showToast("Текст запущен на матрице!"))
      .catch(()=>{});
  }

  function setPreset(txt) {
    document.getElementById('textInput').value = txt;
    applyText();
  }

  function setPresetDash() {
    document.getElementById('textInput').value = '-';
    setSeamless(true);
    updateLoopGap(0);
    applyText();
  }

  function insertEmoji(em) {
    const inp = document.getElementById('textInput');
    inp.value += em;
    inp.focus();
  }

  let seamlessLoop = true;
  let loopGap = 3;

  function setSeamless(val) {
    seamlessLoop = val;
    document.getElementById('loopModeSeamless').classList.toggle('active', seamlessLoop);
    document.getElementById('loopModeClassic').classList.toggle('active', !seamlessLoop);
    document.getElementById('loopGapGroup').style.display = seamlessLoop ? 'flex' : 'none';
    sendTextConfig();
  }

  function updateLoopGap(v) {
    loopGap = parseInt(v);
    document.getElementById('loopGapSlider').value = loopGap;
    document.getElementById('loopGapVal').innerText = v + ' пикс' + (v == 0 ? ' (встык)' : '');
    sendTextConfig();
  }

  let currentStyle = 0;
  let currentBg = 0;

  function setTextMode(m) {
    textState.mode = m;
    for (let i = 0; i < 10; i++) {
      const el = document.getElementById('tm' + i);
      if (el) el.classList.toggle('active', i === m);
    }
    const cp = document.getElementById('textColorPickers');
    const c2 = document.getElementById('tc2Wrap');
    const c3 = document.getElementById('tc3Wrap');
    if (m === 1) { cp.style.display = 'flex'; c2.style.display = 'none'; c3.style.display = 'none'; }
    else if (m === 2) { cp.style.display = 'flex'; c2.style.display = 'inline'; c3.style.display = 'none'; }
    else if (m === 3) { cp.style.display = 'flex'; c2.style.display = 'inline'; c3.style.display = 'inline'; }
    else if (m === 8) { cp.style.display = 'flex'; c2.style.display = 'none'; c3.style.display = 'none'; }
    else { cp.style.display = 'none'; }
    sendTextConfig();
  }

  function setTextStyle(s) {
    currentStyle = s;
    for (let i = 0; i < 6; i++) {
      const el = document.getElementById('ts' + i);
      if (el) el.classList.toggle('active', i === s);
    }
    sendTextConfig();
  }

  function setTextBg(b) {
    currentBg = b;
    for (let i = 0; i < 4; i++) {
      const el = document.getElementById('tbg' + i);
      if (el) el.classList.toggle('active', i === b);
    }
    sendTextConfig();
  }

  function sendTextConfig() {
    let c1 = document.getElementById('tc1').value;
    let c2 = document.getElementById('tc2').value;
    let c3 = document.getElementById('tc3').value;
    fetch('/api/text_params?mode=' + textState.mode +
          '&c1=' + c1.substring(1) +
          '&c2=' + c2.substring(1) +
          '&c3=' + c3.substring(1) +
          '&style=' + currentStyle +
          '&bg=' + currentBg +
          '&speed=' + textState.speed +
          '&seamless=' + (seamlessLoop ? 1 : 0) +
          '&gap=' + loopGap)
      .catch(()=>{});
  }

  function updateTextSpeed(v) {
    textState.speed = parseInt(v);
    document.getElementById('textSpeedVal').innerText = v + ' мс';
    sendTextConfig();
  }

  function toggleTextPause() {
    textState.running = !textState.running;
    document.getElementById('textPauseBtn').innerText = textState.running ? "⏸ Пауза" : "▶ Пуск";
    fetch('/api/text_pause?run=' + (textState.running ? 1 : 0)).catch(()=>{});
  }

  function saveTextSettings() {
    fetch('/api/text_save').then(() => showToast("💾 Текст сохранен во Flash!")).catch(()=>{});
  }

  // --- ГЛОБАЛЬНАЯ ЯРКОСТЬ И НАСТРОЙКИ ---
  let brightTimer = null;
  function updateBright(v) {
    document.getElementById('globalBrightVal').innerText = v;
    clearTimeout(brightTimer);
    brightTimer = setTimeout(() => {
      fetch('/api/brightness?v=' + v).catch(()=>{});
    }, 70);
  }

  function saveGlobalSettings() {
    fetch('/api/save_global').then(() => showToast("💾 Яркость сохранена в Flash")).catch(()=>{});
  }

  function rebootEsp() {
    if (confirm("Перезагрузить ESP32?")) {
      fetch('/api/reboot').then(() => showToast("ESP32 перезагружается..."));
    }
  }

  // Первичная загрузка
  fetch('/api/init')
    .then(r => r.json())
    .then(data => {
      document.getElementById('textInput').value = data.text;
      document.getElementById('globalBright').value = data.brightness;
      document.getElementById('globalBrightVal').innerText = data.brightness;
      document.getElementById('textSpeed').value = data.speed;
      document.getElementById('textSpeedVal').innerText = data.speed + ' мс';
      if (data.effectSpeed) {
        effectSpeedMs = data.effectSpeed;
        document.getElementById('effSpeedSlider').value = effectSpeedMs;
        document.getElementById('effSpeedVal').innerText = effectSpeedMs + ' мс';
      }
      if (data.seamless !== undefined) {
        setSeamless(data.seamless == 1);
      }
      if (data.loopGap !== undefined) {
        loopGap = data.loopGap;
        document.getElementById('loopGapSlider').value = loopGap;
        document.getElementById('loopGapVal').innerText = loopGap + ' пикс' + (loopGap == 0 ? ' (встык)' : '');
      }
      if (data.color1) document.getElementById('tc1').value = '#' + data.color1;
      if (data.color2) document.getElementById('tc2').value = '#' + data.color2;
      if (data.color3) document.getElementById('tc3').value = '#' + data.color3;
      if (data.style !== undefined) setTextStyle(data.style);
      if (data.bg !== undefined) setTextBg(data.bg);
      if (data.blePass) {
        const bpEl = document.getElementById('blePassDisplay');
        if (bpEl) bpEl.innerText = data.blePass;
      }
      setTextMode(data.textMode);
    }).catch(()=>{});

  renderCurrentFrame();
</script>

</body>
</html>
)rawliteral";

// ==========================================
// --- ОБРАБОТЧИКИ API ВЕБ-СЕРВЕРА ---
// ==========================================

void handleInit() {
  String json = "{";
  json += "\"text\":\"" + currentText + "\",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"speed\":" + String(scrollDelay) + ",";
  json += "\"effectSpeed\":" + String(effectSpeed) + ",";
  json += "\"textMode\":" + String(textColMode) + ",";
  json += "\"style\":" + String(textStyle) + ",";
  json += "\"bg\":" + String(textBgMode) + ",";
  json += "\"color1\":\"" + colorToHex(color1) + "\",";
  json += "\"color2\":\"" + colorToHex(color2) + "\",";
  json += "\"color3\":\"" + colorToHex(color3) + "\",";
  json += "\"seamless\":" + String(seamlessLoop ? 1 : 0) + ",";
  json += "\"loopGap\":" + String(loopGap) + ",";
  json += "\"blePass\":\"" + blePassword + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleTextSet() {
  if (server.hasArg("set")) {
    currentText = server.arg("set");
    prepareText(currentText);
    scrollPos = 0;
    appMode = MODE_TEXT;
  }
  server.send(200, "text/plain", "OK");
}

void handleTextParams() {
  if (server.hasArg("mode")) textColMode = constrain(server.arg("mode").toInt(), 0, 9);
  if (server.hasArg("c1")) color1 = parseHexColor(server.arg("c1"));
  if (server.hasArg("c2")) color2 = parseHexColor(server.arg("c2"));
  if (server.hasArg("c3")) color3 = parseHexColor(server.arg("c3"));
  if (server.hasArg("style")) textStyle = constrain(server.arg("style").toInt(), 0, 5);
  if (server.hasArg("bg")) textBgMode = constrain(server.arg("bg").toInt(), 0, 3);
  if (server.hasArg("speed")) scrollDelay = constrain(server.arg("speed").toInt(), 10, 300);
  if (server.hasArg("seamless")) seamlessLoop = (server.arg("seamless").toInt() == 1);
  if (server.hasArg("gap")) loopGap = constrain(server.arg("gap").toInt(), 0, 30);
  appMode = MODE_TEXT;
  server.send(200, "text/plain", "OK");
}

void handleTextPause() {
  if (server.hasArg("run")) {
    textRunning = (server.arg("run").toInt() == 1);
  }
  server.send(200, "text/plain", "OK");
}

void handleTextSave() {
  prefs.putString("text", currentText);
  prefs.putUShort("speed", scrollDelay);
  prefs.putUChar("tmode", textColMode);
  prefs.putUChar("style", textStyle);
  prefs.putUChar("bg", textBgMode);
  prefs.putULong("c1", ((uint32_t)color1.r << 16) | ((uint32_t)color1.g << 8) | color1.b);
  prefs.putULong("c2", ((uint32_t)color2.r << 16) | ((uint32_t)color2.g << 8) | color2.b);
  prefs.putULong("c3", ((uint32_t)color3.r << 16) | ((uint32_t)color3.g << 8) | color3.b);
  prefs.putBool("seamless", seamlessLoop);
  prefs.putUChar("gap", loopGap);
  server.send(200, "text/plain", "OK");
}

void handleEffect() {
  if (server.hasArg("id")) {
    currentEffect = server.arg("id").toInt();
    appMode = MODE_EFFECT;
  }
  if (server.hasArg("spd")) {
    effectSpeed = constrain(server.arg("spd").toInt(), 10, 300);
  }
  server.send(200, "text/plain", "OK");
}

void handleEffectSpeed() {
  if (server.hasArg("spd")) {
    effectSpeed = constrain(server.arg("spd").toInt(), 10, 300);
  }
  server.send(200, "text/plain", "OK");
}

void handleLivePixel() {
  if (server.hasArg("x") && server.hasArg("y") && server.hasArg("c")) {
    uint8_t x = server.arg("x").toInt();
    uint8_t y = server.arg("y").toInt();
    CRGB col = parseHexColor(server.arg("c"));
    if (x < MATRIX_W && y < MATRIX_H) {
      liveDrawBuffer[XY(x, y)] = col;
      leds[XY(x, y)] = col;
      FastLED.show();
      appMode = MODE_DRAW_LIVE;
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleLiveFrame() {
  if (server.hasArg("plain")) {
    String hexData = server.arg("plain");
    int len = hexData.length();
    for (int i = 0; i < NUM_LEDS && (i * 6 + 6) <= len; i++) {
      String hexCol = hexData.substring(i * 6, i * 6 + 6);
      int x = i % MATRIX_W;
      int y = i / MATRIX_W;
      CRGB c = parseHexColor(hexCol);
      liveDrawBuffer[XY(x, y)] = c;
      leds[XY(x, y)] = c;
    }
    FastLED.show();
    appMode = MODE_DRAW_LIVE;
  }
  server.send(200, "text/plain", "OK");
}

void handleAnim() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    int fpsIdx = body.indexOf("\"fps\":");
    if (fpsIdx != -1) {
      customFps = body.substring(fpsIdx + 6, body.indexOf(",", fpsIdx)).toInt();
      if (customFps < 1) customFps = 1;
      if (customFps > 30) customFps = 30;
    }

    int countIdx = body.indexOf("\"count\":");
    int frameCnt = 1;
    if (countIdx != -1) {
      frameCnt = body.substring(countIdx + 8, body.indexOf(",", countIdx)).toInt();
      if (frameCnt > MAX_ANIM_FRAMES) frameCnt = MAX_ANIM_FRAMES;
    }

    int framesArrIdx = body.indexOf("\"frames\":[");
    if (framesArrIdx != -1) {
      customFrameCount = 0;
      int searchPos = framesArrIdx + 9;

      for (int f = 0; f < frameCnt && customFrameCount < MAX_ANIM_FRAMES; f++) {
        int fStart = body.indexOf("[", searchPos);
        int fEnd = body.indexOf("]", fStart);
        if (fStart == -1 || fEnd == -1) break;

        String frameContent = body.substring(fStart + 1, fEnd);
        int pixelIdx = 0;
        int pSearch = 0;

        while (pixelIdx < NUM_LEDS) {
          int q1 = frameContent.indexOf("\"", pSearch);
          if (q1 == -1) break;
          int q2 = frameContent.indexOf("\"", q1 + 1);
          if (q2 == -1) break;

          String hexColor = frameContent.substring(q1 + 1, q2);
          int x = pixelIdx % MATRIX_W;
          int y = pixelIdx / MATRIX_W;
          customFrames[customFrameCount][XY(x, y)] = parseHexColor(hexColor);

          pixelIdx++;
          pSearch = q2 + 1;
        }

        customFrameCount++;
        searchPos = fEnd + 1;
      }
    }

    customCurrentFrame = 0;
    customAnimPlaying = true;
    appMode = MODE_CUSTOM_ANIM;
  }
  server.send(200, "text/plain", "OK");
}

void handleBrightness() {
  if (server.hasArg("v")) {
    brightness = constrain(server.arg("v").toInt(), 0, 255);
    FastLED.setBrightness(brightness);
    FastLED.show();
  }
  server.send(200, "text/plain", "OK");
}

void handleSaveGlobal() {
  prefs.putUChar("bright", brightness);
  server.send(200, "text/plain", "OK");
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

// ==========================================
// --- SETUP & LOOP ---
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n=== ESP32 Matrix Studio Starting ===");

  prefs.begin("matrix", false);
  currentText = prefs.getString("text", "HELLO WORLD!");
  brightness = prefs.getUChar("bright", 40);
  scrollDelay = prefs.getUShort("speed", 65);
  textColMode = prefs.getUChar("tmode", 0);
  textStyle = prefs.getUChar("style", 0);
  textBgMode = prefs.getUChar("bg", 0);
  blePassword = prefs.getString("ble_pass", "1234");
  seamlessLoop = prefs.getBool("seamless", true);
  loopGap = prefs.getUChar("gap", 3);
  uint32_t c1Val = prefs.getULong("c1", 0x00FFFF);
  uint32_t c2Val = prefs.getULong("c2", 0xFF0080);
  uint32_t c3Val = prefs.getULong("c3", 0xFFE600);
  color1 = CRGB((c1Val >> 16) & 0xFF, (c1Val >> 8) & 0xFF, c1Val & 0xFF);
  color2 = CRGB((c2Val >> 16) & 0xFF, (c2Val >> 8) & 0xFF, c2Val & 0xFF);
  color3 = CRGB((c3Val >> 16) & 0xFF, (c3Val >> 8) & 0xFF, c3Val & 0xFF);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();

  prepareText(currentText);

  // Инициализация Bluetooth Low Energy (BLE Nordic UART Service)
  BLEDevice::init("ESP32-Matrix-BLE");
  pBleServer = BLEDevice::createServer();
  pBleServer->setCallbacks(new ServerCallbacks());
  pBleServer->advertiseOnDisconnect(true);
  BLEService *pBleService = pBleServer->createService(BLE_SERVICE_UUID);
  pBleTxChar = pBleService->createCharacteristic(
                 BLE_CHARACTERISTIC_UUID_TX,
                 BLECharacteristic::PROPERTY_NOTIFY
               );
  pBleTxChar->addDescriptor(new BLE2902());
  BLECharacteristic *pBleRxChar = pBleService->createCharacteristic(
                                    BLE_CHARACTERISTIC_UUID_RX,
                                    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
                                  );
  pBleRxChar->setCallbacks(new RxCallbacks());
  pBleService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x10);
  pAdvertising->setMaxPreferred(0x30);
  BLEDevice::startAdvertising();

  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(true);
  WiFi.softAP("ESP32-Matrix");
  dnsServer.start(53, "*", WiFi.softAPIP());
  MDNS.begin("matrix");

  server.on("/", HTTP_GET, []() { server.send(200, "text/html", INDEX_HTML); });
  server.on("/ble", HTTP_GET, []() { server.send(200, "text/html", BLE_APP_HTML); });
  server.on("/api/init", HTTP_GET, handleInit);
  server.on("/api/text", HTTP_GET, handleTextSet);
  server.on("/api/text_params", HTTP_GET, handleTextParams);
  server.on("/api/text_pause", HTTP_GET, handleTextPause);
  server.on("/api/text_save", HTTP_GET, handleTextSave);
  server.on("/api/effect", HTTP_GET, handleEffect);
  server.on("/api/effect_speed", HTTP_GET, handleEffectSpeed);
  server.on("/api/livepixel", HTTP_GET, handleLivePixel);
  server.on("/api/liveframe", HTTP_POST, handleLiveFrame);
  server.on("/api/anim", HTTP_POST, handleAnim);
  server.on("/api/brightness", HTTP_GET, handleBrightness);
  server.on("/api/save_global", HTTP_GET, handleSaveGlobal);
  server.on("/api/reboot", HTTP_GET, handleReboot);

  server.on("/generate_204", []() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.on("/hotspot-detect.html", []() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.on("/ncsi.txt", []() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  bleOldDeviceConnected = bleDeviceConnected;
  unsigned long now = millis();

  // Отложенное согласование стабильных параметров BLE (через 4 сек после подключения)
  static unsigned long bleConnectedAt = 0;
  static bool bleParamsUpdated = false;
  if (bleDeviceConnected) {
    if (bleConnectedAt == 0) bleConnectedAt = now;
    if (!bleParamsUpdated && (now - bleConnectedAt >= 4000)) {
      bleParamsUpdated = true;
      pBleServer->requestConnParams(pBleServer->getConnId(), 24, 48, 0, 1000);
      Serial.println("[BLE] Request conn params: 30-60ms, timeout 10s");
    }
  } else {
    bleConnectedAt = 0;
    bleParamsUpdated = false;
  }

  // BLE Keep-Alive Heartbeat (каждые 4 секунды - поддерживает соединение со смартфоном активным)
  static unsigned long lastBleHeartbeat = 0;
  if (bleDeviceConnected && (now - lastBleHeartbeat >= 4000)) {
    lastBleHeartbeat = now;
    sendBleResponse(bleAuthenticated ? "OK_BEAT" : "PING");
  }

  // 1. РЕЖИМ БЕГУЩЕЙ СТРОКИ / ТЕКСТА V2
  if (appMode == MODE_TEXT) {
    if (now - lastTextStep >= scrollDelay) {
      lastTextStep = now;
      FastLED.clear();
      renderBackground(textBgMode, baseHue, now);

      int effectiveLen = textLength;
      // Если gap == 0 и текст заканчивается 0x00, отсекаем его для абсолютно бесшовного соединения
      if (seamlessLoop && loopGap == 0 && effectiveLen > 1 && textBuffer[effectiveLen - 1] == 0x00) {
        effectiveLen--;
      }

      int period = seamlessLoop ? (effectiveLen + loopGap) : (textLength + MATRIX_W);
      if (period < 1) period = 1;

      // Стили движения: 0:Скролл, 1:Машинка, 2:Волна Y, 3:Дыхание, 4:Глитч, 5:Статичный
      static int twCol = 0;
      static unsigned long twPauseUntil = 0;

      if (textStyle == 1) { // Печатающая машинка
        if (twPauseUntil > 0) {
          if (now >= twPauseUntil) {
            twPauseUntil = 0;
            twCol = 0;
          }
        } else {
          twCol++;
          if (twCol > min(textLength, (int)MATRIX_W) + 2) {
            twPauseUntil = now + 1600;
          }
        }
      }

      for (int x = 0; x < MATRIX_W; x++) {
        uint8_t colData = 0;

        if (textStyle == 5) { // Статичный по центру
          int startX = (textLength < MATRIX_W) ? ((MATRIX_W - textLength) / 2) : 0;
          int colIdx = x - startX;
          if (colIdx >= 0 && colIdx < textLength) {
            colData = textBuffer[colIdx];
          }
        } else if (textStyle == 1) { // Печатающая машинка
          if (x < twCol && x < textLength) {
            colData = textBuffer[x];
          } else if (x == twCol && twPauseUntil == 0) {
            colData = 0x1F; // курсор
          }
        } else if (seamlessLoop) {
          int pos = (scrollPos + x) % period;
          if (pos < effectiveLen) {
            colData = textBuffer[pos];
          }
        } else {
          int colIdx = scrollPos + x - MATRIX_W;
          if (colIdx >= 0 && colIdx < textLength) {
            colData = textBuffer[colIdx];
          }
        }

        // Получаем цвет из 10 палитр
        CRGB color = getTextColor(textColMode, x, baseHue, now);

        // Модификаторы стилей:
        if (textStyle == 3) { // Дыхание / пульс
          uint8_t breath = beatsin8(35, 50, 255);
          color.nscale8(breath);
        } else if (textStyle == 4) { // Глитч
          if (random8() < 6) color = CRGB::White;
          else if (random8() < 12) color = CRGB(255, 0, 100);
        }

        // Волна Y (вертикальное покачивание)
        int yShift = 0;
        if (textStyle == 2) {
          uint8_t s = sin8(x * 25 + baseHue * 3);
          if (s > 170) yShift = 1;
          else if (s < 85) yShift = -1;
        }

        for (int y = 0; y < MATRIX_H; y++) {
          if ((colData >> y) & 1) {
            int targetY = y + yShift;
            if (targetY >= 0 && targetY < MATRIX_H) {
              leds[XY(x, targetY)] = color;
            }
          }
        }
      }

      FastLED.show();
      baseHue += 2;
      if (textRunning && textStyle != 5 && textStyle != 1) {
        scrollPos++;
        if (scrollPos >= period) scrollPos = 0;
      }
    }
  }

  // 2. РЕЖИМ ВСТРОЕННЫХ ЭФФЕКТОВ
  else if (appMode == MODE_EFFECT) {
    if (now - lastEffectStep >= effectSpeed) {
      lastEffectStep = now;
      static uint32_t effectTicks = 0;
      effectTicks++;

      switch (currentEffect) {
        case 0: renderNyanCat(effectTicks); break;
        case 1: renderMatrix(effectTicks); break;
        case 2: renderFire(effectTicks); break;
        case 3: renderEqualizer(effectTicks); break;
        case 4: renderPacman(effectTicks); break;
        case 5: renderHeart(effectTicks); break;
        case 6: renderPlasma(effectTicks); break;
      }

      FastLED.show();
    }
  }

  // 3. РЕЖИМ ПОЛЬЗОВАТЕЛЬСКОЙ ПОКАДРОВОЙ АНИМАЦИИ
  else if (appMode == MODE_CUSTOM_ANIM) {
    uint16_t frameInterval = 1000 / customFps;
    if (now - lastCustomFrameTime >= frameInterval) {
      lastCustomFrameTime = now;

      if (customFrameCount > 0) {
        for (int i = 0; i < NUM_LEDS; i++) {
          leds[i] = customFrames[customCurrentFrame][i];
        }
        FastLED.show();

        if (customAnimPlaying) {
          customCurrentFrame = (customCurrentFrame + 1) % customFrameCount;
        }
      }
    }
  }

  delay(1);
}
