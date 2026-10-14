#include <Preferences.h>
#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define LED_PIN         8
#define NUM_LEDS        100
#define MATRIX_W        20
#define MATRIX_H        5
#define MAX_ANIM_FRAMES 16

CRGB leds[NUM_LEDS];
Preferences prefs;

// --- Режимы работы устройства ---
enum AppMode {
  MODE_IDLE = 0,
  MODE_TEXT = 1,
  MODE_EFFECT = 2,
  MODE_CUSTOM_ANIM = 3,
  MODE_DRAW_LIVE = 4,
  MODE_RAW_STREAM = 5
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
uint8_t textFont = 0;           // 0:Стандарт (5x4), 1:Узкий (5x3), 2:Широкий Bold, 3:7-Сегмент, 4:Ретро 3x3
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

// --- НОРМАЛИЗАЦИЯ СИМВОЛОВ И КИРИЛЛИЦЫ ---
uint32_t normalizeUnicode(uint32_t u) {
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
  return u;
}

// -------------------------------------------------------------
// ШРИФТ 0: СТАНДАРТНЫЙ (Сбалансированный 5x4 / 5x5)
// -------------------------------------------------------------
uint8_t getGlyphColsStandard(uint32_t u, uint8_t* out) {
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
    case 0x0416: out[0]=0x11; out[1]=0x0A; out[2]=0x1F; out[3]=0x0A; out[4]=0x11; return 5; // Ж
    case 0x0417: out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x0A; return 4; // З
    case 0x0418: out[0]=0x1F; out[1]=0x08; out[2]=0x04; out[3]=0x02; out[4]=0x1F; return 5; // И
    case 0x0419: out[0]=0x1E; out[1]=0x08; out[2]=0x05; out[3]=0x02; out[4]=0x1E; return 5; // Й
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
    case '/': out[0]=0x10; out[1]=0x08; out[2]=0x04; out[3]=0x02; out[4]=0x01; return 5;
    case '\\': out[0]=0x01; out[1]=0x02; out[2]=0x04; out[3]=0x08; out[4]=0x10; return 5;
    case '$': out[0]=0x16; out[1]=0x15; out[2]=0x1F; out[3]=0x15; out[4]=0x0D; return 5;
    case '%': out[0]=0x13; out[1]=0x08; out[2]=0x04; out[3]=0x02; out[4]=0x19; return 5;
    case '@': out[0]=0x0E; out[1]=0x11; out[2]=0x17; out[3]=0x15; out[4]=0x0E; return 5;
    case '#': out[0]=0x0A; out[1]=0x1F; out[2]=0x0A; out[3]=0x1F; out[4]=0x0A; return 5;
    case '&': out[0]=0x0A; out[1]=0x15; out[2]=0x15; out[3]=0x0E; out[4]=0x10; return 5;
    case '(': out[0]=0x0E; out[1]=0x11; return 2;
    case ')': out[0]=0x11; out[1]=0x0E; return 2;
    case '[': out[0]=0x1F; out[1]=0x11; return 2;
    case ']': out[0]=0x11; out[1]=0x1F; return 2;
    case '{': out[0]=0x04; out[1]=0x0E; out[2]=0x11; return 3;
    case '}': out[0]=0x11; out[1]=0x0E; out[2]=0x04; return 3;
    case '<': out[0]=0x04; out[1]=0x0A; out[2]=0x11; return 3;
    case '>': out[0]=0x11; out[1]=0x0A; out[2]=0x04; return 3;

    // --- СТРЕЛКИ И УКАЗАТЕЛИ (5x5) ---
    case 0x2190: // ←
      out[0]=0x04; out[1]=0x0E; out[2]=0x15; out[3]=0x04; out[4]=0x04; return 5;
    case 0x2192: // →
      out[0]=0x04; out[1]=0x04; out[2]=0x15; out[3]=0x0E; out[4]=0x04; return 5;
    case 0x2191: // ↑
      out[0]=0x04; out[1]=0x02; out[2]=0x1F; out[3]=0x02; out[4]=0x04; return 5;
    case 0x2193: // ↓
      out[0]=0x04; out[1]=0x08; out[2]=0x1F; out[3]=0x08; out[4]=0x04; return 5;
    case 0x2194: // ↔
      out[0]=0x0E; out[1]=0x15; out[2]=0x04; out[3]=0x15; out[4]=0x0E; return 5;
    case 0x00AB: // «
      out[0]=0x04; out[1]=0x0A; out[2]=0x04; out[3]=0x0A; return 4;
    case 0x00BB: // »
      out[0]=0x0A; out[1]=0x04; out[2]=0x0A; out[3]=0x04; return 4;
    case 0x00B0: // ° градус (квадратик 2х2 в верхнем углу)
      out[0]=0x03; out[1]=0x03; return 2;
    case 0x20BD: // ₽ рубль
      out[0]=0x1F; out[1]=0x0A; out[2]=0x0A; out[3]=0x04; return 4;
    case 0x20AC: // € евро
      out[0]=0x0E; out[1]=0x1F; out[2]=0x15; out[3]=0x11; return 4;
    case 0x00A5: // ¥ китайский юань (5x4)
      out[0]=0x0D; out[1]=0x1E; out[2]=0x1E; out[3]=0x0D; return 4;
    case 0x20B8: // ₸ казахстанский тенге (5x4)
      out[0]=0x03; out[1]=0x1F; out[2]=0x1F; out[3]=0x03; return 4;

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

// -------------------------------------------------------------
// ШРИФТ 1: УЗКИЙ / КОМПАКТНЫЙ (5x3)
// -------------------------------------------------------------
uint8_t getGlyphColsNarrow(uint32_t u, uint8_t* out) {
  switch (u) {
    case 'A': out[0]=0x1E; out[1]=0x05; out[2]=0x1E; return 3;
    case 'B': out[0]=0x1F; out[1]=0x15; out[2]=0x0A; return 3;
    case 'C': out[0]=0x0E; out[1]=0x11; out[2]=0x11; return 3;
    case 'D': out[0]=0x1F; out[1]=0x11; out[2]=0x0E; return 3;
    case 'E': out[0]=0x1F; out[1]=0x15; out[2]=0x11; return 3;
    case 'F': out[0]=0x1F; out[1]=0x05; out[2]=0x01; return 3;
    case 'G': out[0]=0x0E; out[1]=0x11; out[2]=0x1D; return 3;
    case 'H': out[0]=0x1F; out[1]=0x04; out[2]=0x1F; return 3;
    case 'I': out[0]=0x11; out[1]=0x1F; out[2]=0x11; return 3;
    case 'J': out[0]=0x08; out[1]=0x10; out[2]=0x0F; return 3;
    case 'K': out[0]=0x1F; out[1]=0x04; out[2]=0x1B; return 3;
    case 'L': out[0]=0x1F; out[1]=0x10; out[2]=0x10; return 3;
    case 'M': out[0]=0x1F; out[1]=0x02; out[2]=0x1F; return 3;
    case 'N': out[0]=0x1F; out[1]=0x06; out[2]=0x1F; return 3;
    case 'O': out[0]=0x0E; out[1]=0x11; out[2]=0x0E; return 3;
    case 'P': out[0]=0x1F; out[1]=0x05; out[2]=0x02; return 3;
    case 'Q': out[0]=0x0E; out[1]=0x11; out[2]=0x1E; return 3;
    case 'R': out[0]=0x1F; out[1]=0x05; out[2]=0x1A; return 3;
    case 'S': out[0]=0x12; out[1]=0x15; out[2]=0x09; return 3;
    case 'T': out[0]=0x01; out[1]=0x1F; out[2]=0x01; return 3;
    case 'U': out[0]=0x0F; out[1]=0x10; out[2]=0x0F; return 3;
    case 'V': out[0]=0x07; out[1]=0x18; out[2]=0x07; return 3;
    case 'W': out[0]=0x1F; out[1]=0x08; out[2]=0x1F; return 3;
    case 'X': out[0]=0x1B; out[1]=0x04; out[2]=0x1B; return 3;
    case 'Y': out[0]=0x03; out[1]=0x1C; out[2]=0x03; return 3;
    case 'Z': out[0]=0x19; out[1]=0x15; out[2]=0x13; return 3;

    case 0x0411: out[0]=0x1F; out[1]=0x15; out[2]=0x0D; return 3; // Б
    case 0x0413: out[0]=0x1F; out[1]=0x01; out[2]=0x01; return 3; // Г
    case 0x0414: out[0]=0x18; out[1]=0x0F; out[2]=0x0F; out[3]=0x18; return 4; // Д
    case 0x0416: out[0]=0x15; out[1]=0x1F; out[2]=0x15; return 3; // Ж
    case 0x0417: out[0]=0x11; out[1]=0x15; out[2]=0x0A; return 3; // З
    case 0x0418: out[0]=0x1F; out[1]=0x08; out[2]=0x02; out[3]=0x1F; return 4; // И
    case 0x0419: out[0]=0x1E; out[1]=0x09; out[2]=0x02; out[3]=0x1E; return 4; // Й
    case 0x041B: out[0]=0x0F; out[1]=0x01; out[2]=0x1F; return 3; // Л
    case 0x041F: out[0]=0x1F; out[1]=0x01; out[2]=0x1F; return 3; // П
    case 0x0424: out[0]=0x0E; out[1]=0x1F; out[2]=0x0E; return 3; // Ф
    case 0x0426: out[0]=0x0F; out[1]=0x08; out[2]=0x1F; out[3]=0x18; return 4; // Ц
    case 0x0427: out[0]=0x07; out[1]=0x04; out[2]=0x1F; return 3; // Ч
    case 0x0428: out[0]=0x1F; out[1]=0x10; out[2]=0x1F; return 3; // Ш
    case 0x0429: out[0]=0x1F; out[1]=0x10; out[2]=0x1F; out[3]=0x18; return 4; // Щ
    case 0x042A: out[0]=0x01; out[1]=0x1F; out[2]=0x14; out[3]=0x08; return 4; // Ъ
    case 0x042B: out[0]=0x1F; out[1]=0x14; out[2]=0x08; out[3]=0x1F; return 4; // Ы
    case 0x042C: out[0]=0x1F; out[1]=0x14; out[2]=0x08; return 3; // Ь
    case 0x042D: out[0]=0x11; out[1]=0x15; out[2]=0x0E; return 3; // Э
    case 0x042E: out[0]=0x1F; out[1]=0x04; out[2]=0x0E; out[3]=0x11; out[4]=0x0E; return 5; // Ю
    case 0x042F: out[0]=0x16; out[1]=0x09; out[2]=0x1F; return 3; // Я

    case '0': out[0]=0x0E; out[1]=0x11; out[2]=0x0E; return 3;
    case '1': out[0]=0x02; out[1]=0x1F; out[2]=0x00; return 3;
    case '2': out[0]=0x19; out[1]=0x15; out[2]=0x12; return 3;
    case '3': out[0]=0x11; out[1]=0x15; out[2]=0x1F; return 3;
    case '4': out[0]=0x07; out[1]=0x04; out[2]=0x1F; return 3;
    case '5': out[0]=0x17; out[1]=0x15; out[2]=0x1D; return 3;
    case '6': out[0]=0x1F; out[1]=0x15; out[2]=0x1D; return 3;
    case '7': out[0]=0x01; out[1]=0x19; out[2]=0x07; return 3;
    case '8': out[0]=0x1F; out[1]=0x15; out[2]=0x1F; return 3;
    case '9': out[0]=0x17; out[1]=0x15; out[2]=0x1F; return 3;

    case ' ': out[0]=0x00; return 1;
    case '!': out[0]=0x17; return 1;
    case '?': out[0]=0x01; out[1]=0x15; out[2]=0x03; return 3;
    case '.': out[0]=0x10; return 1;
    case ',': out[0]=0x18; return 1;
    case ':': out[0]=0x0A; return 1;
    case ';': out[0]=0x1A; return 1;
    case '-': out[0]=0x04; out[1]=0x04; return 2;
    case '+': out[0]=0x04; out[1]=0x0E; out[2]=0x04; return 3;
    case '=': out[0]=0x0A; out[1]=0x0A; return 2;

    default: return 0; // Fallback
  }
}

// -------------------------------------------------------------
// ШРИФТ 2: ШИРОКИЙ / BOLD (5x5 / 5x6)
// -------------------------------------------------------------
uint8_t getGlyphColsWide(uint32_t u, uint8_t* out) {
  switch (u) {
    case 'A': out[0]=0x1E; out[1]=0x1F; out[2]=0x05; out[3]=0x1F; out[4]=0x1E; return 5;
    case 'B': out[0]=0x1F; out[1]=0x1F; out[2]=0x15; out[3]=0x15; out[4]=0x0E; return 5;
    case 'C': out[0]=0x0E; out[1]=0x1F; out[2]=0x11; out[3]=0x11; out[4]=0x11; return 5;
    case 'D': out[0]=0x1F; out[1]=0x1F; out[2]=0x11; out[3]=0x1F; out[4]=0x0E; return 5;
    case 'E': out[0]=0x1F; out[1]=0x1F; out[2]=0x15; out[3]=0x15; out[4]=0x11; return 5;
    case 'F': out[0]=0x1F; out[1]=0x1F; out[2]=0x05; out[3]=0x05; out[4]=0x01; return 5;
    case 'G': out[0]=0x0E; out[1]=0x1F; out[2]=0x11; out[3]=0x15; out[4]=0x1D; return 5;
    case 'H': out[0]=0x1F; out[1]=0x1F; out[2]=0x04; out[3]=0x1F; out[4]=0x1F; return 5;
    case 'I': out[0]=0x11; out[1]=0x1F; out[2]=0x1F; out[3]=0x11; return 4;
    case 'J': out[0]=0x0C; out[1]=0x10; out[2]=0x10; out[3]=0x1F; out[4]=0x1F; return 5;
    case 'K': out[0]=0x1F; out[1]=0x1F; out[2]=0x04; out[3]=0x0E; out[4]=0x1B; return 5;
    case 'L': out[0]=0x1F; out[1]=0x1F; out[2]=0x10; out[3]=0x10; out[4]=0x10; return 5;
    case 'M': out[0]=0x1F; out[1]=0x1F; out[2]=0x06; out[3]=0x06; out[4]=0x1F; out[5]=0x1F; return 6;
    case 'N': out[0]=0x1F; out[1]=0x1F; out[2]=0x03; out[3]=0x0C; out[4]=0x1F; out[5]=0x1F; return 6;
    case 'O': out[0]=0x0E; out[1]=0x1F; out[2]=0x11; out[3]=0x1F; out[4]=0x0E; return 5;
    case 'P': out[0]=0x1F; out[1]=0x1F; out[2]=0x05; out[3]=0x07; out[4]=0x02; return 5;
    case 'Q': out[0]=0x0E; out[1]=0x1F; out[2]=0x11; out[3]=0x1F; out[4]=0x1E; return 5;
    case 'R': out[0]=0x1F; out[1]=0x1F; out[2]=0x05; out[3]=0x0F; out[4]=0x16; return 5;
    case 'S': out[0]=0x12; out[1]=0x17; out[2]=0x15; out[3]=0x1D; out[4]=0x09; return 5;
    case 'T': out[0]=0x01; out[1]=0x01; out[2]=0x1F; out[3]=0x1F; out[4]=0x01; out[5]=0x01; return 6;
    case 'U': out[0]=0x0F; out[1]=0x1F; out[2]=0x10; out[3]=0x1F; out[4]=0x0F; return 5;
    case 'V': out[0]=0x07; out[1]=0x1E; out[2]=0x18; out[3]=0x1E; out[4]=0x07; return 5;
    case 'W': out[0]=0x1F; out[1]=0x1F; out[2]=0x0C; out[3]=0x0C; out[4]=0x1F; out[5]=0x1F; return 6;
    case 'X': out[0]=0x1B; out[1]=0x1F; out[2]=0x0E; out[3]=0x1F; out[4]=0x1B; return 5;
    case 'Y': out[0]=0x03; out[1]=0x07; out[2]=0x1C; out[3]=0x07; out[4]=0x03; return 5;
    case 'Z': out[0]=0x19; out[1]=0x1D; out[2]=0x17; out[3]=0x13; out[4]=0x11; return 5;

    case 0x0411: out[0]=0x1F; out[1]=0x1F; out[2]=0x15; out[3]=0x15; out[4]=0x0D; return 5; // Б
    case 0x0413: out[0]=0x1F; out[1]=0x1F; out[2]=0x01; out[3]=0x01; out[4]=0x01; return 5; // Г
    case 0x0414: out[0]=0x18; out[1]=0x1F; out[2]=0x09; out[3]=0x09; out[4]=0x1F; out[5]=0x18; return 6; // Д
    case 0x0416: out[0]=0x11; out[1]=0x1B; out[2]=0x1F; out[3]=0x1F; out[4]=0x1B; out[5]=0x11; return 6; // Ж
    case 0x0417: out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x1F; out[4]=0x0E; return 5; // З
    case 0x0418: out[0]=0x1F; out[1]=0x1F; out[2]=0x08; out[3]=0x04; out[4]=0x1F; out[5]=0x1F; return 6; // И
    case 0x0419: out[0]=0x1E; out[1]=0x1F; out[2]=0x09; out[3]=0x05; out[4]=0x1F; out[5]=0x1E; return 6; // Й
    case 0x041B: out[0]=0x0F; out[1]=0x1F; out[2]=0x01; out[3]=0x1F; out[4]=0x1F; return 5; // Л
    case 0x041F: out[0]=0x1F; out[1]=0x1F; out[2]=0x01; out[3]=0x1F; out[4]=0x1F; return 5; // П
    case 0x0424: out[0]=0x04; out[1]=0x0E; out[2]=0x1F; out[3]=0x1F; out[4]=0x0E; out[5]=0x04; return 6; // Ф
    case 0x0426: out[0]=0x0F; out[1]=0x1F; out[2]=0x08; out[3]=0x1F; out[4]=0x1F; out[5]=0x18; return 6; // Ц
    case 0x0427: out[0]=0x07; out[1]=0x07; out[2]=0x04; out[3]=0x1F; out[4]=0x1F; return 5; // Ч
    case 0x0428: out[0]=0x1F; out[1]=0x1F; out[2]=0x10; out[3]=0x1F; out[4]=0x1F; out[5]=0x1F; return 6; // Ш
    case 0x0429: out[0]=0x1F; out[1]=0x1F; out[2]=0x10; out[3]=0x1F; out[4]=0x1F; out[5]=0x18; return 6; // Щ
    case 0x042A: out[0]=0x01; out[1]=0x1F; out[2]=0x1F; out[3]=0x14; out[4]=0x0E; return 5; // Ъ
    case 0x042B: out[0]=0x1F; out[1]=0x1F; out[2]=0x14; out[3]=0x0E; out[4]=0x1F; out[5]=0x1F; return 6; // Ы
    case 0x042C: out[0]=0x1F; out[1]=0x1F; out[2]=0x14; out[3]=0x14; out[4]=0x0E; return 5; // Ь
    case 0x042D: out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x1F; out[4]=0x0E; return 5; // Э
    case 0x042E: out[0]=0x1F; out[1]=0x1F; out[2]=0x04; out[3]=0x0E; out[4]=0x11; out[5]=0x0E; return 6; // Ю
    case 0x042F: out[0]=0x16; out[1]=0x1F; out[2]=0x0D; out[3]=0x1F; out[4]=0x1F; return 5; // Я

    case '0': out[0]=0x0E; out[1]=0x1F; out[2]=0x11; out[3]=0x1F; out[4]=0x0E; return 5;
    case '1': out[0]=0x04; out[1]=0x0E; out[2]=0x1F; out[3]=0x1F; out[4]=0x00; return 5;
    case '2': out[0]=0x19; out[1]=0x1D; out[2]=0x15; out[3]=0x17; out[4]=0x13; return 5;
    case '3': out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x1F; out[4]=0x0E; return 5;
    case '4': out[0]=0x07; out[1]=0x07; out[2]=0x04; out[3]=0x1F; out[4]=0x1F; return 5;
    case '5': out[0]=0x17; out[1]=0x17; out[2]=0x15; out[3]=0x1D; out[4]=0x1D; return 5;
    case '6': out[0]=0x0E; out[1]=0x1F; out[2]=0x15; out[3]=0x1D; out[4]=0x1C; return 5;
    case '7': out[0]=0x01; out[1]=0x01; out[2]=0x19; out[3]=0x0F; out[4]=0x03; return 5;
    case '8': out[0]=0x0E; out[1]=0x1F; out[2]=0x15; out[3]=0x1F; out[4]=0x0E; return 5;
    case '9': out[0]=0x07; out[1]=0x17; out[2]=0x15; out[3]=0x1F; out[4]=0x0E; return 5;

    case ' ': out[0]=0x00; out[1]=0x00; out[2]=0x00; return 3;
    case '!': out[0]=0x17; out[1]=0x17; return 2;
    case '?': out[0]=0x01; out[1]=0x1D; out[2]=0x15; out[3]=0x07; return 4;
    case '-': out[0]=0x04; out[1]=0x04; out[2]=0x04; out[3]=0x04; return 4;
    case '+': out[0]=0x04; out[1]=0x04; out[2]=0x1F; out[3]=0x04; out[4]=0x04; return 5;

    default: return 0; // Fallback
  }
}

// -------------------------------------------------------------
// ШРИФТ 3: ЦИФРОВОЙ / 7-СЕГМЕНТНЫЙ (5x4)
// -------------------------------------------------------------
uint8_t getGlyphColsDigital(uint32_t u, uint8_t* out) {
  switch (u) {
    case '0': out[0]=0x1F; out[1]=0x11; out[2]=0x11; out[3]=0x1F; return 4;
    case '1': out[0]=0x00; out[1]=0x00; out[2]=0x1F; out[3]=0x00; return 4;
    case '2': out[0]=0x1D; out[1]=0x15; out[2]=0x15; out[3]=0x17; return 4;
    case '3': out[0]=0x11; out[1]=0x15; out[2]=0x15; out[3]=0x1F; return 4;
    case '4': out[0]=0x07; out[1]=0x04; out[2]=0x04; out[3]=0x1F; return 4;
    case '5': out[0]=0x17; out[1]=0x15; out[2]=0x15; out[3]=0x1D; return 4;
    case '6': out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x1D; return 4;
    case '7': out[0]=0x01; out[1]=0x01; out[2]=0x01; out[3]=0x1F; return 4;
    case '8': out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x1F; return 4;
    case '9': out[0]=0x17; out[1]=0x15; out[2]=0x15; out[3]=0x1F; return 4;

    case 'A': out[0]=0x1F; out[1]=0x05; out[2]=0x05; out[3]=0x1F; return 4;
    case 'B': out[0]=0x1F; out[1]=0x14; out[2]=0x14; out[3]=0x1C; return 4;
    case 'C': out[0]=0x1F; out[1]=0x11; out[2]=0x11; out[3]=0x11; return 4;
    case 'D': out[0]=0x1C; out[1]=0x14; out[2]=0x14; out[3]=0x1F; return 4;
    case 'E': out[0]=0x1F; out[1]=0x15; out[2]=0x15; out[3]=0x15; return 4;
    case 'F': out[0]=0x1F; out[1]=0x05; out[2]=0x05; out[3]=0x01; return 4;
    case 'H': out[0]=0x1F; out[1]=0x04; out[2]=0x04; out[3]=0x1F; return 4;
    case 'L': out[0]=0x1F; out[1]=0x10; out[2]=0x10; out[3]=0x10; return 4;
    case 'O': out[0]=0x1F; out[1]=0x11; out[2]=0x11; out[3]=0x1F; return 4;
    case 'P': out[0]=0x1F; out[1]=0x05; out[2]=0x05; out[3]=0x07; return 4;
    case 'U': out[0]=0x1F; out[1]=0x10; out[2]=0x10; out[3]=0x1F; return 4;

    case ':': out[0]=0x0A; return 1;
    case '.': out[0]=0x10; return 1;
    case '-': out[0]=0x04; out[1]=0x04; out[2]=0x04; return 3;

    default: return 0; // Fallback
  }
}

// -------------------------------------------------------------
// ШРИФТ 4: РЕТРО / ARCADE TINY (3x3 по центру)
// -------------------------------------------------------------
uint8_t getGlyphColsTiny(uint32_t u, uint8_t* out) {
  switch (u) {
    case 'A': out[0]=0x0C; out[1]=0x06; out[2]=0x0C; return 3;
    case 'B': out[0]=0x0E; out[1]=0x0A; out[2]=0x04; return 3;
    case 'C': out[0]=0x0E; out[1]=0x0A; out[2]=0x0A; return 3;
    case 'D': out[0]=0x0E; out[1]=0x0A; out[2]=0x04; return 3;
    case 'E': out[0]=0x0E; out[1]=0x0E; out[2]=0x0A; return 3;
    case 'F': out[0]=0x0E; out[1]=0x06; out[2]=0x02; return 3;
    case 'G': out[0]=0x0E; out[1]=0x0A; out[2]=0x0E; return 3;
    case 'H': out[0]=0x0E; out[1]=0x04; out[2]=0x0E; return 3;
    case 'I': out[0]=0x0E; return 1;
    case 'J': out[0]=0x08; out[1]=0x08; out[2]=0x06; return 3;
    case 'K': out[0]=0x0E; out[1]=0x04; out[2]=0x0A; return 3;
    case 'L': out[0]=0x0E; out[1]=0x08; out[2]=0x08; return 3;
    case 'M': out[0]=0x0E; out[1]=0x02; out[2]=0x0E; return 3;
    case 'N': out[0]=0x0E; out[1]=0x06; out[2]=0x0E; return 3;
    case 'O': out[0]=0x0E; out[1]=0x0A; out[2]=0x0E; return 3;
    case 'P': out[0]=0x0E; out[1]=0x06; out[2]=0x06; return 3;
    case 'Q': out[0]=0x0E; out[1]=0x0A; out[2]=0x0C; return 3;
    case 'R': out[0]=0x0E; out[1]=0x06; out[2]=0x08; return 3;
    case 'S': out[0]=0x06; out[1]=0x0E; out[2]=0x0A; return 3;
    case 'T': out[0]=0x02; out[1]=0x0E; out[2]=0x02; return 3;
    case 'U': out[0]=0x0E; out[1]=0x08; out[2]=0x0E; return 3;
    case 'V': out[0]=0x06; out[1]=0x08; out[2]=0x06; return 3;
    case 'W': out[0]=0x0E; out[1]=0x08; out[2]=0x0E; return 3;
    case 'X': out[0]=0x0A; out[1]=0x04; out[2]=0x0A; return 3;
    case 'Y': out[0]=0x06; out[1]=0x0C; out[2]=0x06; return 3;
    case 'Z': out[0]=0x0A; out[1]=0x0E; out[2]=0x0A; return 3;

    case '0': out[0]=0x0E; out[1]=0x0A; out[2]=0x0E; return 3;
    case '1': out[0]=0x04; out[1]=0x0E; return 2;
    case '2': out[0]=0x0A; out[1]=0x0E; out[2]=0x06; return 3;
    case '3': out[0]=0x0A; out[1]=0x0A; out[2]=0x0E; return 3;
    case '4': out[0]=0x06; out[1]=0x04; out[2]=0x0E; return 3;
    case '5': out[0]=0x0E; out[1]=0x0E; out[2]=0x0A; return 3;
    case '6': out[0]=0x0E; out[1]=0x0E; out[2]=0x0E; return 3;
    case '7': out[0]=0x02; out[1]=0x02; out[2]=0x0E; return 3;
    case '8': out[0]=0x0E; out[1]=0x04; out[2]=0x0E; return 3;
    case '9': out[0]=0x06; out[1]=0x0E; out[2]=0x0E; return 3;

    case ' ': out[0]=0x00; return 1;
    case '!': out[0]=0x0A; return 1;
    case '.': out[0]=0x08; return 1;
    case '-': out[0]=0x04; out[1]=0x04; return 2;

    default: return 0; // Fallback
  }
}

// -------------------------------------------------------------
// ГЛАВНЫЙ ДИСПЕТЧЕР ШРИФТОВ С АВТОМАТИЧЕСКИМ FALLBACK
// -------------------------------------------------------------
uint8_t getGlyphCols(uint32_t u, uint8_t* out, uint8_t font) {
  u = normalizeUnicode(u);
  uint8_t w = 0;
  switch (font) {
    case 1: w = getGlyphColsNarrow(u, out); break;
    case 2: w = getGlyphColsWide(u, out); break;
    case 3: w = getGlyphColsDigital(u, out); break;
    case 4: w = getGlyphColsTiny(u, out); break;
    default: w = getGlyphColsStandard(u, out); break;
  }
  if (w == 0) {
    w = getGlyphColsStandard(u, out);
  }
  return w;
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

    uint8_t w = getGlyphCols(unicode, temp, textFont);
    for (uint8_t j = 0; j < w; j++) {
      textBuffer[textLength++] = temp[j];
    }
    if (unicode != ' ') {
      textBuffer[textLength++] = 0x00;
    }
  }

  // Убираем завершающий пробел у последнего символа для точного центрирования
  if (textLength > 0 && textBuffer[textLength - 1] == 0x00) {
    textLength--;
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

// -------------------------------------------------------------
// ОТОБРАЖЕНИЕ 10 СТОЛБИКОВ ЭКВАЛАЙЗЕРА С ПРОМЕЖУТКОМ В 1 ПИКСЕЛЬ
// -------------------------------------------------------------
void renderVuBars(uint8_t style, const String& heights) {
  FastLED.clear();
  int numBars = min((int)heights.length(), 10);
  for (int b = 0; b < numBars; b++) {
    int col = b * 2; // Четные колонки (0, 2, 4, ... 18) - 10 столбиков
    // Нечетные колонки (1, 3, 5, ... 19) остаются 0x00 (1px черный промежуток)
    int h = heights.charAt(b) - '0';
    h = constrain(h, 0, 5);

    for (int lvl = 1; lvl <= h; lvl++) {
      int y = 5 - lvl; // lvl 1 -> y=4 (низ), lvl 5 -> y=0 (верх)
      CRGB c;
      if (style == 0) { // Студийный VU: Зеленый -> Лайм -> Желтый -> Оранжевый -> Красный
        if (lvl == 1) c = CRGB(0, 255, 30);
        else if (lvl == 2) c = CRGB(160, 255, 0);
        else if (lvl == 3) c = CRGB(255, 200, 0);
        else if (lvl == 4) c = CRGB(255, 80, 0);
        else c = CRGB(255, 0, 0);
      } else if (style == 1) { // Cyberpunk Neon: Синий -> Циан -> Пурпур -> Розовый -> Белый
        if (lvl == 1) c = CRGB(0, 100, 255);
        else if (lvl == 2) c = CRGB(0, 240, 255);
        else if (lvl == 3) c = CRGB(200, 0, 255);
        else if (lvl == 4) c = CRGB(255, 0, 150);
        else c = CRGB(255, 255, 255);
      } else if (style == 2) { // Fire: Бордовый -> Оранжевый -> Желтый -> Белый огонь
        if (lvl == 1) c = CRGB(180, 0, 0);
        else if (lvl == 2) c = CRGB(255, 40, 0);
        else if (lvl == 3) c = CRGB(255, 120, 0);
        else if (lvl == 4) c = CRGB(255, 210, 0);
        else c = CRGB(255, 255, 220);
      } else if (style == 3) { // Ice: Глубокий синий -> Аква -> Голубой -> Белый лед
        if (lvl == 1) c = CRGB(0, 40, 180);
        else if (lvl == 2) c = CRGB(0, 140, 255);
        else if (lvl == 3) c = CRGB(0, 240, 255);
        else if (lvl == 4) c = CRGB(150, 255, 255);
        else c = CRGB(255, 255, 255);
      } else { // Rainbow: Градиент по частотным колонкам
        c = CHSV(b * 25, 255, 160 + lvl * 19);
      }
      leds[XY(col, y)] = c;
    }
  }
  FastLED.show();
}

uint8_t lastRenderedEffect = 255;
uint32_t effectTicks = 0;

void switchAppMode(AppMode newMode) {
  if (appMode != newMode) {
    appMode = newMode;
    FastLED.clear();
    FastLED.show();
    scrollPos = 0;
    lastTextStep = 0;
    lastEffectStep = 0;
    lastCustomFrameTime = 0;
    lastRenderedEffect = 255;
    effectTicks = 0;
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
    switchAppMode(MODE_TEXT);
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
      if (tCount >= 10) {
        textFont = constrain(tokens[9].toInt(), 0, 4);
      }
      prepareText(currentText);
      switchAppMode(MODE_TEXT);
      sendBleResponse("OK_TP");
    }
  } else if (cmd.startsWith("FONT:")) {
    textFont = constrain(cmd.substring(5).toInt(), 0, 4);
    prepareText(currentText);
    sendBleResponse("OK_FONT;" + String(textFont));
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
    switchAppMode(MODE_EFFECT);
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
        switchAppMode(MODE_DRAW_LIVE);
      }
    }
    sendBleResponse("OK_LP");
  } else if (cmd.startsWith("SPEED:")) {
    uint16_t spd = constrain(cmd.substring(6).toInt(), 10, 300);
    scrollDelay = spd;
    effectSpeed = spd;
    customFps = constrain(1000 / spd, 1, 30);
    prefs.putUShort("speed", scrollDelay);
    sendBleResponse("OK_SPEED;" + String(spd));
  } else if (cmd == "STOP") {
    switchAppMode(MODE_IDLE);
    sendBleResponse("OK_STOP");
  } else if (cmd == "SAVE_SETTINGS") {
    prefs.putUChar("bright", brightness);
    prefs.putUShort("speed", scrollDelay);
    sendBleResponse("OK_SAVE_SETTINGS");
  } else if (cmd.startsWith("VU:")) {
    // VU:<style>,<10_digits> e.g. VU:0,0123454321
    int comma = cmd.indexOf(',', 3);
    uint8_t style = 0;
    String hStr;
    if (comma != -1) {
      style = cmd.substring(3, comma).toInt();
      hStr = cmd.substring(comma + 1);
    } else {
      hStr = cmd.substring(3);
    }
    switchAppMode(MODE_RAW_STREAM);
    renderVuBars(style, hStr);
  } else if (cmd.startsWith("RAW:")) {
    String hex = cmd.substring(4);
    int len = hex.length();
    for (int i = 0; i < NUM_LEDS && (i * 6 + 6) <= len; i++) {
      int x = i % MATRIX_W;
      int y = i / MATRIX_W;
      leds[XY(x, y)] = parseHexColor(hex.substring(i * 6, i * 6 + 6));
    }
    FastLED.show();
    switchAppMode(MODE_RAW_STREAM);
  } else if (cmd == "STATUS?") {
    String st = "STATUS:{\"text\":\"" + currentText + "\",";
    st += "\"bright\":" + String(brightness) + ",";
    st += "\"speed\":" + String(scrollDelay) + ",";
    st += "\"appMode\":" + String((int)appMode) + ",";
    st += "\"eff\":" + String(currentEffect) + ",";
    st += "\"mode\":" + String(textColMode) + ",";
    st += "\"style\":" + String(textStyle) + ",";
    st += "\"bg\":" + String(textBgMode) + ",";
    st += "\"c1\":\"" + colorToHex(color1) + "\",";
    st += "\"c2\":\"" + colorToHex(color2) + "\",";
    st += "\"c3\":\"" + colorToHex(color3) + "\",";
    st += "\"seamless\":" + String(seamlessLoop ? 1 : 0) + ",";
    st += "\"gap\":" + String(loopGap) + ",";
    st += "\"font\":" + String(textFont) + "}";
    sendBleResponse(st);
  } else if (cmd == "SAVE_TEXT") {
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
    prefs.putUChar("font", textFont);
    sendBleResponse("OK_SAVE_TEXT");
  } else if (cmd == "SAVE_BRIGHT") {
    prefs.putUChar("bright", brightness);
    sendBleResponse("OK_SAVE_BRIGHT");
  } else if (cmd == "REBOOT") {
    sendBleResponse("OK_REBOOT");
    delay(200);
    ESP.restart();
  } else if (cmd == "ANIM_CLR") {
    customFrameCount = 0;
    sendBleResponse("OK_ANIM_CLR");
  } else if (cmd.startsWith("ANIM_FRAME:")) {
    // ANIM_FRAME:frame_idx,hex600
    int c1 = cmd.indexOf(',', 11);
    if (c1 != -1) {
      uint8_t fIdx = cmd.substring(11, c1).toInt();
      if (fIdx < MAX_ANIM_FRAMES) {
        String hex = cmd.substring(c1 + 1);
        int len = hex.length();
        for (int i = 0; i < NUM_LEDS && (i * 6 + 6) <= len; i++) {
          int x = i % MATRIX_W;
          int y = i / MATRIX_W;
          customFrames[fIdx][XY(x, y)] = parseHexColor(hex.substring(i * 6, i * 6 + 6));
        }
        if (fIdx >= customFrameCount) customFrameCount = fIdx + 1;
        sendBleResponse("OK_FRAME;" + String(fIdx));
      }
    }
  } else if (cmd.startsWith("ANIM_PLAY:")) {
    customFps = constrain(cmd.substring(10).toInt(), 1, 30);
    customCurrentFrame = 0;
    customAnimPlaying = true;
    switchAppMode(MODE_CUSTOM_ANIM);
    sendBleResponse("OK_ANIM_PLAY");
  } else if (cmd == "ANIM_STOP") {
    customAnimPlaying = false;
    switchAppMode(MODE_TEXT);
    sendBleResponse("OK_ANIM_STOP");
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
// --- SETUP & LOOP (PURE BLE MODE) ---
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n=== ESP32 Matrix BLE Studio Starting ===");

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
  textFont = prefs.getUChar("font", 0);
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

  Serial.println("[BLE] Pure BLE Mode Ready. Advertising started!");
}

void loop() {
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

  // 0. РЕЖИМ ОЖИДАНИЯ / ВЫКЛЮЧЕНО (MODE_IDLE)
  if (appMode == MODE_IDLE) {
    // Ничего не рендерим
  }
  // 1. РЕЖИМ БЕГУЩЕЙ СТРОКИ / ТЕКСТА V2
  else if (appMode == MODE_TEXT) {
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
    if (currentEffect != lastRenderedEffect) {
      lastRenderedEffect = currentEffect;
      effectTicks = 0;
      FastLED.clear();
      FastLED.show();
    }

    if (now - lastEffectStep >= effectSpeed) {
      lastEffectStep = now;
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
