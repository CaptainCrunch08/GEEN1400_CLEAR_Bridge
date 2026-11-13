/*
  GEEN1400 teacher device
  Seeed XIAO ESP32-S3 Sense and a 2.4 inch touch LCD (240x320, ILI9341,
  XPT2046). Yes, No, and Next are buttons drawn on the glass.

  Board in the Arduino IDE: XIAO_ESP32S3
  PSRAM: OPI PSRAM
  USB CDC On Boot: Enabled
  USB Mode: USB-OTG (TinyUSB). That is the board default. Hardware CDC
  mode cannot show the week as a USB drive.
  Partition: Default with spiffs
  Library: LovyanGFX (Library Manager)

  Wiring. The Sense microSD socket already uses D8, D9, and D10, so the
  screen stays off those pins. Tie the LCD RST pin to 3V3. The touch
  chip shares the LCD clock and data pins. Its own select and data-out
  wires use the pins that used to be the Yes and No buttons.

  LCD VCC  3V3
  LCD GND  GND
  LCD DIN  D5   GPIO6     also touch T_DIN
  LCD CLK  D4   GPIO5     also touch T_CLK
  LCD CS   D3   GPIO4
  LCD DC   D2   GPIO3
  LCD RST  3V3
  LCD BL   D0   GPIO1
  Touch T_DO   D1   GPIO2
  Touch T_CS   D6   GPIO43
  Touch T_IRQ  not connected

  Blank button  D7  GPIO44  to GND. Press to black the glass. Press
  again to bring the picture back. Power stays on.
  The gear in the top right opens Look. Brightness and light or dark
  are saved there. Full power on this backlight is harsh, so the bright
  end of the slider stays below full power.
  Buzzer        D8  GPIO7   active or passive buzzer to GND.
  D8 is the Sense SD clock. This sketch holds the SD card off, so the
  pin is free. Take a card out of the socket before using the buzzer.
  One second: a student choice. Two short: an accommodation.
  Two short then a longer one: Help, or I need my teacher.
  Yes or Not yet after a strategy is saved in the week and does not buzz.

  A student panel sends the name, the issue, and the request or solution
  over Bluetooth. This screen opens on the oldest one. Next moves to the
  following student. Yes and No answer the student on screen and send
  that answer back.

  When CLEAR_Teacher_Config.h is filled in, completed drawings are sent
  over Wi-Fi to the local OCR computer and recognized text returns over BLE.

  The week is kept in the chip's flash. The drive does not appear until
  the teacher enters the class PIN and the teacher code. CLEAR.CSV on
  that drive is locked with the class PIN. Copying the file leaves the
  week on the device. Deleting CLEAR.CSV and unplugging starts a new
  empty week. A panel that has not been allowed yet is held on a waiting
  list. Its message is not shown and is not written into the week.

  Neither ESP32 has a battery clock. The student panel keeps the time,
  starting from the moment its sketch was uploaded, and sends that time
  with every selection. This board adopts that time while it stays on,
  and a Yes or No stores when the reply happened. The website turns
  those times into the computer's local time and into how long the
  reply took. Time spent while a board is unplugged is not counted.
*/

#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <LovyanGFX.hpp>
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <esp_heap_caps.h>
#include "CLEAR_Link.h"
#include "CLEAR_Seal.h"
#if __has_include("CLEAR_Teacher_Config.h")
#include "CLEAR_Teacher_Config.h"
#endif

#ifndef CLEAR_WIFI_SSID
#define CLEAR_WIFI_SSID ""
#endif
#ifndef CLEAR_WIFI_PASSWORD
#define CLEAR_WIFI_PASSWORD ""
#endif
#ifndef CLEAR_OCR_SERVER_URL
#define CLEAR_OCR_SERVER_URL ""
#endif
#ifndef CLEAR_OCR_TOKEN
#define CLEAR_OCR_TOKEN ""
#endif

#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE != 1
#include "USB.h"
#include "USBMSC.h"
#define CLEAR_USB_DRIVE 1
#else
#define CLEAR_USB_DRIVE 0
#endif

static const int PIN_LCD_MOSI = 6;
static const int PIN_LCD_SCK = 5;
static const int PIN_LCD_CS = 4;
static const int PIN_LCD_DC = 3;
static const int PIN_LCD_BL = 1;
static const int PIN_SD_CS = 21;
static const int PIN_TOUCH_MISO = 2;
static const int PIN_TOUCH_CS = 43;
static const int PIN_BLANK = 44;
static const int PIN_BUZZ = 7;

static const int BUTTON_Y = 268;
static const int BUTTON_H = 52;
static const int BUTTON_W = 80;
static const int YES_X = 0;
static const int NO_X = 80;
static const int NEXT_X = 160;

static uint16_t COL_BG = 0xF7BD;
static uint16_t COL_FG = 0x2610;
static uint16_t COL_TRY = 0x6F9B;
static uint16_t COL_ASK = 0xFE70;
static uint16_t COL_NO = 0xFBE4;
static uint16_t COL_FOOT = 0xE73C;

// The glass stays covered until the teacher finishes both codes.
// After that, ten seconds with no touch covers the student message again.
static const int MODE_MAKE_PIN = 1;
static const int MODE_MAKE_PIN_AGAIN = 2;
static const int MODE_MAKE_CODE = 3;
static const int MODE_MAKE_CODE_AGAIN = 4;
static const int MODE_ENTER_PIN = 5;
static const int MODE_ENTER_CODE = 6;
static const int MODE_QUEUE = 7;
static const int MODE_COVER = 8;
static const int MODE_PENDING = 9;
static const int MODE_TRUSTED = 10;
static const int MODE_LOOK = 11;

static const int GEAR_X = 200;
static const int GEAR_Y = 2;
static const int GEAR_W = 36;
static const int GEAR_H = 32;
static const int BRIGHT_MIN = 20;
static const int BRIGHT_MAX = 100;
static const unsigned long PRIVACY_IDLE_MS = 10000;
static const int TRUSTED_MAX = 12;
static const int PENDING_MAX = 8;

static const char *LOG_PATH = "/clear.csv";
static const char *LOG_HEADER =
  "id,time_sent,student,feeling,choice,kind,teacher_response,time_answered\n";

static const int LIST_MAX = 12;
static const int RING_MAX = 8;
static const int DRAWING_RING_MAX = 16;
static const unsigned long BUTTON_GAP_MS = 200;
static const uint32_t DISK_SECTORS = 8192;
static const uint32_t DISK_BYTES = DISK_SECTORS * 512;
static const uint16_t DISK_FAT_SECTORS = 6;
static const uint16_t DISK_ROOT_ENTRIES = 128;
static const uint16_t DISK_ROOT_SECTORS = DISK_ROOT_ENTRIES / 16;
static const uint16_t DISK_SECTORS_PER_CLUSTER = 4;
static const uint16_t DISK_DATA_START_SECTOR =
  1 + DISK_FAT_SECTORS + DISK_ROOT_SECTORS;
static const uint16_t DISK_CLUSTER_BYTES = DISK_SECTORS_PER_CLUSTER * 512;
static const uint16_t DISK_MAX_FILES = DISK_ROOT_ENTRIES;
static const size_t DRAWING_MAX_BYTES = 300000;
static const uint8_t OCR_QUEUE_MAX = 4;

struct DrawingPacketEntry {
  uint8_t bytes[sizeof(ClearDrawingChunk)];
  uint8_t length;
};

struct DiskFileEntry {
  char path[48];
  char shortName[11];
  uint32_t size;
  uint16_t firstCluster;
};

// US Mountain time, so the small screen matches a Colorado classroom.
// The file itself stores UTC. The website uses the computer's time zone.
static const char *CLOCK_TZ = "MST7MDT,M3.2.0,M11.1.0";

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_ILI9341 _panel;
  lgfx::Touch_XPT2046 _touch;
  lgfx::Light_PWM _light;
public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 20000000;
      cfg.freq_read = 2500000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_TOUCH_MISO;
      cfg.pin_dc = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = true;
      cfg.bus_shared = true;
      _panel.config(cfg);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 400;
      cfg.y_max = 3900;
      cfg.pin_int = -1;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI2_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_TOUCH_MISO;
      cfg.pin_cs = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    {
      // PWM on the backlight pin. A higher number is brighter.
      // Channel 1 stays clear of the buzzer, which uses channel 7.
      auto cfg = _light.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.freq = 20000;
      cfg.pwm_channel = 1;
      cfg.invert = false;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;
static bool storageOk = false;
static bool usbMounted = false;
static int viewIndex = 0;
static int waitingCount = 0;
static ClearNote waiting[LIST_MAX];
static ClearNote noticeRing[RING_MAX];
static volatile uint8_t ringWrite = 0;
static uint8_t ringRead = 0;
static DrawingPacketEntry drawingRing[DRAWING_RING_MAX];
static volatile uint8_t drawingRingWrite = 0;
static uint8_t drawingRingRead = 0;
static volatile bool drawingQueueOverflow = false;
static volatile uint32_t drawingOverflowId = 0;
static uint32_t activeDrawingId = 0;
static uint32_t activeDrawingBytes = 0;
static uint32_t activeDrawingReceived = 0;
static uint16_t activeDrawingChunkIndex = 0;
static uint8_t activeDrawingBoard = 0;
static File activeDrawingFile;
static char activeDrawingTempPath[40] = "";
static char activeDrawingPath[40] = "";
struct OcrJob {
  char path[40];
  char remoteJobId[40];
  uint32_t drawingId;
  uint8_t board;
  bool serverJobStarted;
  bool textReady;
  uint16_t textSize;
  unsigned long remoteJobStartedMs;
  unsigned long nextRemotePollMs;
  char text[601];
};
enum OcrProcessResult {
  OCR_PROCESS_READY,
  OCR_PROCESS_WAITING,
  OCR_PROCESS_RETRY,
  OCR_PROCESS_FAILED
};

static OcrJob ocrJobs[OCR_QUEUE_MAX] = {};
static uint8_t ocrJobWrite = 0;
static uint8_t ocrJobRead = 0;
static bool ocrJobOverflow = false;
static portMUX_TYPE ocrAckMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool ocrAckReady = false;
static volatile uint32_t ocrAckDrawingId = 0;
static volatile uint16_t ocrAckChunkIndex = 0;
static volatile bool ocrAckAccepted = false;
static unsigned long nextOcrWifiRetryMs = 0;
static unsigned long nextOcrAttemptMs = 0;
static bool ocrConfigWarningShown = false;
static DiskFileEntry diskFiles[DISK_MAX_FILES];
static uint16_t diskFileCount = 0;
static unsigned long buttonMs = 0;
static bool touchWasDown = false;
static BLECharacteristic *replyChar = nullptr;
static uint8_t *disk = nullptr;
static Preferences privacyPrefs;
static int screenMode = MODE_ENTER_PIN;
static int lookReturn = MODE_ENTER_PIN;
static int teacherBright = 40;
static bool teacherDark = false;
static char classPin[5] = "";
static char teacherCode[5] = "";
static char pinEntry[5] = "";
static char pinFirst[5] = "";
static char privacyNote[48] = "";
static bool secretsReady = false;
static bool servicesStarted = false;
static unsigned long activityMs = 0;
static char trustedNames[TRUSTED_MAX][17];
static int trustedCount = 0;
static int trustedView = 0;
static ClearNote pendingNotes[PENDING_MAX];
static int pendingCount = 0;
static int pendingView = 0;
static String weekPlain;

#if CLEAR_USB_DRIVE
static USBMSC MSC;
#endif

static void copyField(char *dest, int size, const char *src) {
  int i = 0;
  if (src == nullptr) {
    src = "";
  }
  while (src[i] != 0 && i < size - 1) {
    dest[i] = src[i];
    i++;
  }
  dest[i] = 0;
}

static void drawWrapped(const char *text, int x, int y, int maxWidth, int bottom) {
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(COL_FG, COL_BG);
  int cursorX = x;
  int cursorY = y;
  const char *word = text;
  while (word != nullptr && *word != 0 && cursorY < bottom) {
    while (*word == ' ') {
      word++;
    }
    if (*word == 0) {
      break;
    }
    const char *end = word;
    while (*end != 0 && *end != ' ') {
      end++;
    }
    char buf[48];
    int len = end - word;
    if (len > 47) {
      len = 47;
    }
    for (int i = 0; i < len; i++) {
      buf[i] = word[i];
    }
    buf[len] = 0;
    int width = lcd.textWidth(buf);
    if (cursorX > x && cursorX + width > x + maxWidth) {
      cursorX = x;
      cursorY += 22;
    }
    if (cursorY >= bottom) {
      break;
    }
    lcd.drawString(buf, cursorX, cursorY);
    cursorX += width + lcd.textWidth(" ");
    word = end;
  }
}

static void formatSent(uint32_t unixTime, char *out, int size) {
  if (unixTime < 1000000000UL) {
    copyField(out, size, "No time yet");
    return;
  }
  time_t stamp = unixTime;
  struct tm parts;
  localtime_r(&stamp, &parts);
  strftime(out, size, "%b %d  %I:%M %p", &parts);
}

static void copyPin(char *dest, const char *src) {
  for (int i = 0; i < 4; i++) {
    dest[i] = src[i];
  }
  dest[4] = 0;
}

static bool fourDigits(const char *text) {
  if (text == nullptr) {
    return false;
  }
  for (int i = 0; i < 4; i++) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  return text[4] == 0;
}

static uint32_t classPasskey() {
  return (uint32_t)atoi(classPin);
}

static void clearEntry() {
  pinEntry[0] = 0;
}

static bool nameIsTrusted(const char *name) {
  for (int i = 0; i < trustedCount; i++) {
    if (strcmp(trustedNames[i], name) == 0) {
      return true;
    }
  }
  return false;
}

static void saveTrusted() {
  String packed;
  for (int i = 0; i < trustedCount; i++) {
    if (i > 0) {
      packed += "\n";
    }
    packed += trustedNames[i];
  }
  privacyPrefs.begin("clear-teacher", false);
  privacyPrefs.putString("trusted", packed);
  privacyPrefs.end();
}

static void loadPrivacy() {
  privacyPrefs.begin("clear-teacher", true);
  String pin = privacyPrefs.getString("cpin", "");
  String code = privacyPrefs.getString("tcode", "");
  String packed = privacyPrefs.getString("trusted", "");
  teacherBright = privacyPrefs.getInt("bright", 40);
  teacherDark = privacyPrefs.getInt("dark", 0) != 0;
  privacyPrefs.end();
  if (teacherBright < BRIGHT_MIN) {
    teacherBright = BRIGHT_MIN;
  }
  if (teacherBright > BRIGHT_MAX) {
    teacherBright = BRIGHT_MAX;
  }
  teacherBright = (teacherBright / 10) * 10;
  applyLook();
  secretsReady = fourDigits(pin.c_str()) && fourDigits(code.c_str());
  if (secretsReady) {
    copyPin(classPin, pin.c_str());
    copyPin(teacherCode, code.c_str());
    screenMode = MODE_ENTER_PIN;
    copyField(privacyNote, sizeof(privacyNote), "Enter the class PIN");
  } else {
    classPin[0] = 0;
    teacherCode[0] = 0;
    screenMode = MODE_MAKE_PIN;
    copyField(privacyNote, sizeof(privacyNote), "Choose a class PIN");
  }
  trustedCount = 0;
  int start = 0;
  while (trustedCount < TRUSTED_MAX && start < packed.length()) {
    int end = packed.indexOf('\n', start);
    if (end < 0) {
      end = packed.length();
    }
    String name = packed.substring(start, end);
    name.trim();
    if (name.length() > 0) {
      copyField(trustedNames[trustedCount], sizeof(trustedNames[0]), name.c_str());
      trustedCount++;
    }
    start = end + 1;
  }
  clearEntry();
}

static void saveSecrets() {
  privacyPrefs.begin("clear-teacher", false);
  privacyPrefs.putString("cpin", classPin);
  privacyPrefs.putString("tcode", teacherCode);
  privacyPrefs.end();
  secretsReady = true;
}

static int findPending(const char *name) {
  for (int i = 0; i < pendingCount; i++) {
    if (strcmp(pendingNotes[i].student, name) == 0) {
      return i;
    }
  }
  return -1;
}

static void holdPending(const ClearNote *note) {
  int slot = findPending(note->student);
  if (slot < 0) {
    if (pendingCount >= PENDING_MAX) {
      slot = 0;
    } else {
      slot = pendingCount;
      pendingCount++;
    }
  }
  pendingNotes[slot] = *note;
  if (screenMode == MODE_QUEUE || screenMode == MODE_PENDING) {
    screenMode = MODE_PENDING;
    pendingView = slot;
  }
}

class ClearSecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override {
    return classPasskey();
  }
  void onPassKeyNotify(uint32_t pass_key) override {
    (void)pass_key;
  }
  bool onSecurityRequest() override {
    return secretsReady;
  }
  bool onConfirmPIN(uint32_t pin) override {
    return pin == classPasskey();
  }
};

static void paintTapButton(int x, const char *label, uint16_t fill);
static void finishSignIn();
static void applyLook();
static void paintGear();

static void paintPrivacyKeys() {
  static const char *labels[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "<"
  };
  const int padX = 2;
  const int padY = 96;
  const int padW = 76;
  const int padH = 50;
  const int gap = 4;
  for (int i = 0; i < 12; i++) {
    int x = padX + (i % 3) * (padW + gap);
    int y = padY + (i / 3) * (padH + gap);
    lcd.fillRoundRect(x, y, padW, padH, 8, COL_FOOT);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, COL_FOOT);
    lcd.drawString(labels[i], x + padW / 2, y + padH / 2);
  }
}

static void paintPrivacy() {
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(COL_FG, COL_BG);
  if (screenMode == MODE_PENDING) {
    lcd.setFont(&fonts::Font2);
    lcd.drawString("Allow this panel?", 12, 16);
    if (pendingCount > 0) {
      if (pendingView < 0 || pendingView >= pendingCount) {
        pendingView = 0;
      }
      lcd.setFont(&fonts::Font4);
      lcd.drawString(pendingNotes[pendingView].student, 12, 52);
      lcd.setFont(&fonts::Font2);
      char countBuf[24];
      snprintf(countBuf, sizeof(countBuf), "%d waiting", pendingCount);
      lcd.drawString(countBuf, 12, 96);
      drawWrapped("Messages stay hidden until you allow the panel.", 12, 124, 216, 240);
    }
    paintTapButton(YES_X, "Allow", COL_TRY);
    paintTapButton(NO_X, "Not now", COL_NO);
    paintTapButton(NEXT_X, "Next", COL_FOOT);
    paintGear();
    return;
  }
  if (screenMode == MODE_TRUSTED) {
    lcd.setFont(&fonts::Font2);
    lcd.drawString("Allowed panels", 12, 16);
    if (trustedCount < 1) {
      lcd.drawString("None yet", 12, 52);
    } else {
      if (trustedView < 0 || trustedView >= trustedCount) {
        trustedView = 0;
      }
      lcd.setFont(&fonts::Font4);
      lcd.drawString(trustedNames[trustedView], 12, 52);
      lcd.setFont(&fonts::Font2);
      lcd.drawString("Remove forgets this panel.", 12, 96);
    }
    paintTapButton(YES_X, "Remove", COL_NO);
    paintTapButton(NO_X, "Back", COL_FOOT);
    paintTapButton(NEXT_X, "Next", COL_TRY);
    paintGear();
    return;
  }
  const char *title = "Class PIN";
  if (screenMode == MODE_MAKE_CODE || screenMode == MODE_MAKE_CODE_AGAIN ||
      screenMode == MODE_ENTER_CODE) {
    title = "Teacher code";
  } else if (screenMode == MODE_COVER) {
    title = "Hidden";
  }
  lcd.setFont(&fonts::Font4);
  lcd.drawString(title, 12, 12);
  lcd.setFont(&fonts::Font2);
  lcd.drawString(privacyNote, 12, 44);
  lcd.setFont(&fonts::Font4);
  char dots[8] = "_ _ _ _";
  int filled = (int)strlen(pinEntry);
  if (filled > 4) {
    filled = 4;
  }
  for (int i = 0; i < filled; i++) {
    dots[i * 2] = '*';
  }
  lcd.drawString(dots, 12, 66);
  paintPrivacyKeys();
  paintGear();
}

static void acceptDigits() {
  if (!fourDigits(pinEntry)) {
    return;
  }
  if (screenMode == MODE_MAKE_PIN) {
    copyPin(pinFirst, pinEntry);
    clearEntry();
    screenMode = MODE_MAKE_PIN_AGAIN;
    copyField(privacyNote, sizeof(privacyNote), "Enter the class PIN again");
  } else if (screenMode == MODE_MAKE_PIN_AGAIN) {
    if (strcmp(pinEntry, pinFirst) != 0) {
      clearEntry();
      pinFirst[0] = 0;
      screenMode = MODE_MAKE_PIN;
      copyField(privacyNote, sizeof(privacyNote), "Those did not match");
    } else {
      copyPin(classPin, pinEntry);
      clearEntry();
      screenMode = MODE_MAKE_CODE;
      copyField(privacyNote, sizeof(privacyNote), "Choose the teacher code");
    }
  } else if (screenMode == MODE_MAKE_CODE) {
    copyPin(pinFirst, pinEntry);
    clearEntry();
    screenMode = MODE_MAKE_CODE_AGAIN;
    copyField(privacyNote, sizeof(privacyNote), "Enter the teacher code again");
  } else if (screenMode == MODE_MAKE_CODE_AGAIN) {
    if (strcmp(pinEntry, pinFirst) != 0) {
      clearEntry();
      screenMode = MODE_MAKE_CODE;
      copyField(privacyNote, sizeof(privacyNote), "Those did not match");
    } else {
      copyPin(teacherCode, pinEntry);
      clearEntry();
      saveSecrets();
      finishSignIn();
    }
  } else if (screenMode == MODE_ENTER_PIN || screenMode == MODE_COVER) {
    if (strcmp(pinEntry, classPin) != 0 && !clearTestPin(pinEntry)) {
      clearEntry();
      copyField(privacyNote, sizeof(privacyNote), "Try the class PIN again");
    } else if (screenMode == MODE_COVER) {
      clearEntry();
      screenMode = MODE_QUEUE;
      activityMs = millis();
    } else {
      clearEntry();
      screenMode = MODE_ENTER_CODE;
      copyField(privacyNote, sizeof(privacyNote), "Enter the teacher code");
    }
  } else if (screenMode == MODE_ENTER_CODE) {
    if (strcmp(pinEntry, teacherCode) != 0) {
      clearEntry();
      copyField(privacyNote, sizeof(privacyNote), "Try the teacher code again");
    } else {
      clearEntry();
      finishSignIn();
    }
  }
}

static bool privacyKeyAt(int x, int y, char *label) {
  const int padX = 2;
  const int padY = 96;
  const int padW = 76;
  const int padH = 50;
  const int gap = 4;
  static const char *labels[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "<"
  };
  for (int i = 0; i < 12; i++) {
    int keyX = padX + (i % 3) * (padW + gap);
    int keyY = padY + (i / 3) * (padH + gap);
    if (x >= keyX && x < keyX + padW && y >= keyY && y < keyY + padH) {
      *label = labels[i][0];
      return true;
    }
  }
  return false;
}

static void typePrivacyKey(char key) {
  int n = (int)strlen(pinEntry);
  if (key == 'C') {
    clearEntry();
  } else if (key == '<') {
    if (n > 0) {
      pinEntry[n - 1] = 0;
    }
  } else if (key >= '0' && key <= '9' && n < 4) {
    pinEntry[n] = key;
    pinEntry[n + 1] = 0;
  }
  if (fourDigits(pinEntry)) {
    acceptDigits();
  }
}

static void paintTapButton(int x, const char *label, uint16_t fill) {
  lcd.fillRect(x, BUTTON_Y, BUTTON_W, BUTTON_H, fill);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  uint16_t ink = teacherDark ? COL_FG : (uint16_t)0x0000;
  lcd.setTextColor(ink, fill);
  lcd.drawString(label, x + BUTTON_W / 2, BUTTON_Y + BUTTON_H / 2);
}

static void paintMinuteButton() {
  lcd.fillRect(NEXT_X, BUTTON_Y, BUTTON_W, BUTTON_H, COL_FOOT);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(teacherDark ? COL_FG : (uint16_t)0x0000, COL_FOOT);
  lcd.drawString("In a", NEXT_X + BUTTON_W / 2, BUTTON_Y + 18);
  lcd.drawString("minute", NEXT_X + BUTTON_W / 2, BUTTON_Y + 36);
}

static bool viewingAid() {
  return waitingCount > 0 && viewIndex >= 0 && viewIndex < waitingCount &&
         waiting[viewIndex].asksTeacher == CLEAR_ASK_AID;
}

static bool screenBlank = false;
static bool blankWasDown = false;
static unsigned long blankMs = 0;
static int buzzStep = -1;
static unsigned long buzzUntil = 0;
static const unsigned *buzzPlan = nullptr;
static const unsigned BUZZ_ACT[] = {1000, 0};
static const unsigned BUZZ_AID[] = {140, 90, 140, 0};
static const unsigned BUZZ_HELP[] = {140, 90, 140, 90, 520, 0};

static void buzzWrite(bool on) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_BUZZ, on ? 128 : 0);
#else
  ledcWrite(7, on ? 128 : 0);
#endif
}

static void startBuzz(int kind) {
  if (kind == 2) buzzPlan = BUZZ_HELP;
  else if (kind == 1) buzzPlan = BUZZ_AID;
  else buzzPlan = BUZZ_ACT;
  buzzStep = 0;
  buzzUntil = millis() + buzzPlan[0];
  buzzWrite(true);
}

static void buzzFor(const ClearNote *note) {
  if (strcmp(note->choice, "I need help") == 0 || strcmp(note->choice, "I need my teacher") == 0) {
    startBuzz(2);
  } else if (note->asksTeacher == CLEAR_ASK_AID) {
    startBuzz(1);
  } else {
    startBuzz(0);
  }
}

static void serviceBuzz() {
  if (buzzStep < 0 || buzzPlan == nullptr) {
    return;
  }
  if ((long)(millis() - buzzUntil) < 0) {
    return;
  }
  buzzStep++;
  unsigned dur = buzzPlan[buzzStep];
  if (dur == 0) {
    buzzWrite(false);
    buzzStep = -1;
    return;
  }
  buzzWrite((buzzStep % 2) == 0);
  buzzUntil = millis() + dur;
}

static void serviceBlank() {
  bool down = digitalRead(PIN_BLANK) == LOW;
  if (down && !blankWasDown && (millis() - blankMs) > 300) {
    blankMs = millis();
    screenBlank = !screenBlank;
    if (screenBlank) {
      lcd.setBrightness(0);
      lcd.fillScreen(0x0000);
    } else {
      applyBrightness();
      paintScreen();
    }
  }
  blankWasDown = down;
}

// Tell the student, and add a week-file line, without growing the note.
// Pending means the device has the request. Seen means it is on screen.
static void notifyReply(ClearNote note, uint8_t answer);

static void reportStatus(ClearNote note, uint8_t answer) {
  note.kind = CLEAR_KIND_REPLY;
  note.answer = answer;
  notifyReply(note, answer);
  appendRow(&note, clockNow());
}

// The note on the glass has been acknowledged. Ones still in the list
// stay pending until Next brings them up, or until the screen is on.
static void markCurrentSeen() {
  if (screenBlank || screenMode != MODE_QUEUE || waitingCount < 1) {
    return;
  }
  if (viewIndex < 0 || viewIndex >= waitingCount) {
    viewIndex = 0;
  }
  uint8_t state = waiting[viewIndex].answer;
  if (state == CLEAR_ANSWER_SEEN) {
    return;
  }
  if (state != CLEAR_ANSWER_NONE && state != CLEAR_ANSWER_GOT) {
    return;
  }
  waiting[viewIndex].answer = CLEAR_ANSWER_SEEN;
  reportStatus(waiting[viewIndex], CLEAR_ANSWER_SEEN);
}

// 100% on the slider is 150 of 255. Full power on this panel hurts to look at.
static int dutyForBrightness(int pct) {
  if (pct < BRIGHT_MIN) {
    pct = BRIGHT_MIN;
  }
  if (pct > BRIGHT_MAX) {
    pct = BRIGHT_MAX;
  }
  return 36 + (114 * (pct - BRIGHT_MIN)) / (BRIGHT_MAX - BRIGHT_MIN);
}

static void applyBrightness() {
  if (screenBlank) {
    lcd.setBrightness(0);
    return;
  }
  lcd.setBrightness(dutyForBrightness(teacherBright));
}

static void applyLook() {
  if (teacherDark) {
    COL_BG = 0x1082;
    COL_FG = 0xE71C;
    COL_TRY = 0x1B46;
    COL_ASK = 0xA280;
    COL_NO = 0x600C;
    COL_FOOT = 0x3186;
  } else {
    COL_BG = 0xF7BD;
    COL_FG = 0x2610;
    COL_TRY = 0x6F9B;
    COL_ASK = 0xFE70;
    COL_NO = 0xFBE4;
    COL_FOOT = 0xE73C;
  }
}

static void saveLook() {
  privacyPrefs.begin("clear-teacher", false);
  privacyPrefs.putInt("bright", teacherBright);
  privacyPrefs.putInt("dark", teacherDark ? 1 : 0);
  privacyPrefs.end();
}

static void setBright(int pct) {
  if (pct < BRIGHT_MIN) {
    pct = BRIGHT_MIN;
  }
  if (pct > BRIGHT_MAX) {
    pct = BRIGHT_MAX;
  }
  pct = (pct / 10) * 10;
  if (pct == teacherBright) {
    return;
  }
  teacherBright = pct;
  applyBrightness();
  saveLook();
}

static void paintGear() {
  uint16_t btn = teacherDark ? (uint16_t)0x39E7 : (uint16_t)0xDEDB;
  uint16_t metal = teacherDark ? (uint16_t)0xBDF7 : (uint16_t)0x4A69;
  lcd.fillRoundRect(GEAR_X, GEAR_Y, GEAR_W, GEAR_H, 8, btn);
  int cx = GEAR_X + GEAR_W / 2;
  int cy = GEAR_Y + GEAR_H / 2;
  for (int i = 0; i < 8; i++) {
    float angle = i * 3.14159f / 4.0f;
    int tx = cx + (int)(cosf(angle) * 6.0f);
    int ty = cy + (int)(sinf(angle) * 6.0f);
    lcd.fillCircle(tx, ty, 2, metal);
  }
  lcd.fillCircle(cx, cy, 5, metal);
  lcd.fillCircle(cx, cy, 2, btn);
}

static bool gearHit(int x, int y) {
  return x >= GEAR_X && x < GEAR_X + GEAR_W && y >= GEAR_Y && y < GEAR_Y + GEAR_H;
}

static void paintLook() {
  lcd.fillScreen(COL_BG);
  lcd.fillRoundRect(148, 8, 80, 36, 8, COL_TRY);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_TRY);
  lcd.drawString("Done", 188, 26);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Look", 12, 26);

  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Brightness", 12, 60);
  lcd.fillRoundRect(8, 86, 36, 36, 8, COL_FOOT);
  lcd.fillRoundRect(196, 86, 36, 36, 8, COL_FOOT);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(COL_FG, COL_FOOT);
  lcd.drawString("-", 26, 104);
  lcd.drawString("+", 214, 104);
  lcd.fillRoundRect(52, 96, 136, 16, 6, COL_FOOT);
  int fillW = 136 * (teacherBright - BRIGHT_MIN) / (BRIGHT_MAX - BRIGHT_MIN);
  if (fillW < 0) {
    fillW = 0;
  }
  if (fillW > 136) {
    fillW = 136;
  }
  if (fillW > 0) {
    lcd.fillRoundRect(52, 96, fillW, 16, 6, COL_TRY);
  }
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", teacherBright);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString(pct, 120, 136);

  uint16_t lightFill = teacherDark ? COL_FOOT : COL_TRY;
  uint16_t darkFill = teacherDark ? COL_TRY : COL_FOOT;
  lcd.fillRoundRect(12, 176, 104, 44, 8, lightFill);
  lcd.fillRoundRect(124, 176, 104, 44, 8, darkFill);
  lcd.setTextColor(COL_FG, lightFill);
  lcd.drawString("Light", 64, 198);
  lcd.setTextColor(COL_FG, darkFill);
  lcd.drawString("Dark", 176, 198);
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintScreen() {
  if (screenBlank) {
    lcd.fillScreen(0x0000);
    return;
  }
  if (screenMode == MODE_LOOK) {
    paintLook();
    return;
  }
  if (screenMode != MODE_QUEUE) {
    paintPrivacy();
    return;
  }
  markCurrentSeen();
  lcd.fillScreen(COL_BG);
  if (viewingAid()) {
    paintTapButton(YES_X, "Accept", COL_TRY);
    paintTapButton(NO_X, "Decline", COL_NO);
    paintMinuteButton();
  } else {
    paintTapButton(YES_X, "Yes", COL_TRY);
    paintTapButton(NO_X, "No", COL_NO);
    paintTapButton(NEXT_X, "Next", COL_FOOT);
  }

  if (waitingCount < 1) {
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(COL_FG, COL_BG);
    lcd.drawString("Waiting for a student", 12, 24);
    paintGear();
    return;
  }
  if (viewIndex < 0 || viewIndex >= waitingCount) {
    viewIndex = 0;
  }
  const ClearNote *note = &waiting[viewIndex];
  uint16_t band = note->asksTeacher ? COL_ASK : COL_TRY;
  lcd.fillRect(0, 0, 240, 36, band);
  lcd.setTextColor(COL_FG, band);
  lcd.drawString(note->asksTeacher ? "Ask" : "Try", 12, 18);
  char countBuf[16];
  snprintf(countBuf, sizeof(countBuf), "%d of %d", viewIndex + 1, waitingCount);
  lcd.setTextDatum(textdatum_t::middle_right);
  lcd.drawString(countBuf, GEAR_X - 6, 18);

  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(note->student, 12, 48);
  lcd.setFont(&fonts::Font2);
  char when[28];
  formatSent(note->timeSent, when, sizeof(when));
  lcd.drawString(when, 12, 80);
  lcd.drawString("Issue", 12, 112);
  lcd.drawString(note->feeling, 12, 134);
  lcd.drawString(note->asksTeacher ? "Request" : "Solution", 12, 168);
  drawWrapped(note->choice, 12, 190, 216, 236);
  const char *state = "Pending";
  if (note->answer == CLEAR_ANSWER_SEEN) {
    state = "Seen";
  }
  lcd.setTextDatum(textdatum_t::middle_right);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString(state, 228, 252);
  lcd.setTextDatum(textdatum_t::top_left);
  if (pendingCount > 0) {
    char waitBuf[32];
    snprintf(waitBuf, sizeof(waitBuf), "%d panel%s waiting", pendingCount,
             pendingCount == 1 ? "" : "s");
    lcd.setFont(&fonts::Font2);
    lcd.drawString(waitBuf, 12, 244);
  }
  paintGear();
}

static bool refreshDisk();

static bool saveWeekPlain() {
  if (!fourDigits(classPin)) {
    File file = SPIFFS.open(LOG_PATH, FILE_WRITE);
    if (!file) {
      return false;
    }
    file.print(weekPlain);
    file.close();
    return true;
  }
  String sealed;
  if (!clearSealText(classPin, weekPlain, sealed)) {
    return false;
  }
  File file = SPIFFS.open(LOG_PATH, FILE_WRITE);
  if (!file) {
    return false;
  }
  file.print(sealed);
  file.close();
  if (disk != nullptr) {
    refreshDisk();
  }
  return true;
}

static bool loadWeekPlain() {
  File file = SPIFFS.open(LOG_PATH, FILE_READ);
  if (!file) {
    weekPlain = LOG_HEADER;
    return true;
  }
  String raw = file.readString();
  file.close();
  if (raw.startsWith("CLEARENC1")) {
    return clearOpenText(classPin, raw, weekPlain);
  }
  weekPlain = raw;
  return true;
}

static void normalizeWeekPlain() {
  int split = weekPlain.indexOf('\n');
  String header = split < 0 ? weekPlain : weekPlain.substring(0, split);
  header.trim();
  if (header.indexOf("time_answered") >= 0) {
    return;
  }
  String rest = split < 0 ? "" : weekPlain.substring(split + 1);
  String rebuilt = LOG_HEADER;
  int start = 0;
  while (start < rest.length()) {
    int end = rest.indexOf('\n', start);
    if (end < 0) {
      end = rest.length();
    }
    String line = rest.substring(start, end);
    line.trim();
    if (line.length() > 0) {
      rebuilt += line;
      rebuilt += ",\n";
    }
    if (end >= rest.length()) {
      break;
    }
    start = end + 1;
  }
  weekPlain = rebuilt;
}

static bool writeFreshLog() {
  weekPlain = LOG_HEADER;
  return saveWeekPlain();
}

// No battery clock. A saved time covers a reboot. Wi-Fi, when the OCR
// network is up, moves the clock forward by the hours the power was off.
static bool ntpStarted = false;
static bool clockSynced = false;
static unsigned long clockSaveMs = 0;
static const char *CLOCK_TZ = "MST7MDT,M3.2.0,M11.1.0";

static void saveTeacherClock() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return;
  }
  privacyPrefs.begin("clear-teacher", false);
  privacyPrefs.putUInt("epoch", (uint32_t)now);
  privacyPrefs.end();
}

static void startTeacherClock() {
  setenv("TZ", CLOCK_TZ, 1);
  tzset();
  privacyPrefs.begin("clear-teacher", true);
  uint32_t saved = privacyPrefs.getUInt("epoch", 0);
  privacyPrefs.end();
  if (saved < 1700000000UL) {
    return;
  }
  struct timeval tv;
  tv.tv_sec = saved;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  clockSaveMs = millis();
}

static void serviceNetworkClock() {
  if (CLEAR_WIFI_SSID[0] == 0) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if ((long)(millis() - nextOcrWifiRetryMs) >= 0) {
      WiFi.begin(CLEAR_WIFI_SSID, CLEAR_WIFI_PASSWORD);
      nextOcrWifiRetryMs = millis() + 30000;
    }
    return;
  }
  if (!ntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ntpStarted = true;
  }
  if (!clockSynced && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    clockSynced = true;
    saveTeacherClock();
    clockSaveMs = millis();
  }
}

// A student message carries the time of the tap. Until the network
// clock is set, this board catches up to that time. After that, the
// network time stays in charge.
static void adoptStudentClock(uint32_t timeSent) {
  if (clockSynced || timeSent < 1700000000UL) {
    return;
  }
  time_t now = time(nullptr);
  if ((uint32_t)now >= timeSent) {
    return;
  }
  struct timeval tv;
  tv.tv_sec = timeSent;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

// Zero means the board does not know the time yet.
static uint32_t clockNow() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return 0;
  }
  return (uint32_t)now;
}

// timeAnswered is 0 for the first line of a selection. A Yes or No
// writes a second line with the same id and the time of the tap.
static void appendRow(const ClearNote *note, uint32_t timeAnswered) {
  if (!storageOk || !loadWeekPlain()) {
    return;
  }
  const char *answer = "";
  if (note->answer == CLEAR_ANSWER_YES) {
    answer = "yes";
  } else if (note->answer == CLEAR_ANSWER_NO) {
    answer = "no";
  } else if (note->answer == CLEAR_ANSWER_LATER) {
    answer = "minute";
  } else if (note->answer == CLEAR_ANSWER_GOT) {
    answer = "pending";
  } else if (note->answer == CLEAR_ANSWER_SEEN) {
    answer = "seen";
  }
  weekPlain += note->eventId;
  weekPlain += ',';
  if (note->timeSent > 0) {
    weekPlain += note->timeSent;
  }
  weekPlain += ",\"";
  weekPlain += note->student;
  weekPlain += "\",\"";
  weekPlain += note->feeling;
  weekPlain += "\",\"";
  weekPlain += note->choice;
  weekPlain += "\",";
  const char *kindWord = "try";
  if (note->asksTeacher == CLEAR_ASK_HELPED) kindWord = "helped";
  else if (note->asksTeacher == CLEAR_ASK_NOTYET) kindWord = "notyet";
  else if (note->asksTeacher) kindWord = "ask";
  weekPlain += kindWord;
  weekPlain += ',';
  weekPlain += answer;
  weekPlain += ',';
  if (timeAnswered > 0) {
    weekPlain += timeAnswered;
  }
  weekPlain += "\n";
  // Keep the newest lines when the week file gets large, so old student
  // notes do not stay on this device forever.
  if (weekPlain.length() > 48000) {
    int headerEnd = weekPlain.indexOf('\n');
    int cut = headerEnd + 1;
    int drop = (int)weekPlain.length() / 2;
    while (cut < drop) {
      int next = weekPlain.indexOf('\n', cut);
      if (next < 0) {
        break;
      }
      cut = next + 1;
    }
    if (headerEnd >= 0 && cut > headerEnd) {
      weekPlain = weekPlain.substring(0, headerEnd + 1) + weekPlain.substring(cut);
    }
  }
  saveWeekPlain();
}

// Older week files have no time_answered column. Add an empty one so
// later Yes and No lines can store a time without a mismatched header.
static void ensureLogHeader() {
  File file = SPIFFS.open(LOG_PATH, FILE_READ);
  if (!file) {
    return;
  }
  String header = file.readStringUntil('\n');
  if (header.startsWith("CLEARENC1")) {
    file.close();
    return;
  }
  header.trim();
  if (header.indexOf("time_answered") >= 0) {
    file.close();
    return;
  }
  String rest = file.readString();
  file.close();
  File out = SPIFFS.open(LOG_PATH, FILE_WRITE);
  if (!out) {
    return;
  }
  out.print(LOG_HEADER);
  int start = 0;
  while (start < rest.length()) {
    int end = rest.indexOf('\n', start);
    if (end < 0) {
      end = rest.length();
    }
    String line = rest.substring(start, end);
    line.trim();
    if (line.length() > 0) {
      out.print(line);
      out.println(",");
    }
    if (end >= rest.length()) {
      break;
    }
    start = end + 1;
  }
  out.close();
}

static void startStorage() {
  storageOk = SPIFFS.begin(true);
  if (!storageOk) {
    return;
  }
  if (!SPIFFS.exists(LOG_PATH)) {
    storageOk = writeFreshLog();
  }
  if (storageOk) {
    ensureLogHeader();
  }
}

static void fat12Set(uint8_t *fat, int cluster, uint16_t value) {
  int offset = cluster + (cluster / 2);
  if (cluster & 1) {
    fat[offset] = (fat[offset] & 0x0F) | ((value << 4) & 0xF0);
    fat[offset + 1] = (value >> 4) & 0xFF;
  } else {
    fat[offset] = value & 0xFF;
    fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
  }
}

static bool addDiskFile(const char *path, const char shortName[11],
                        uint8_t *fat, uint8_t *root,
                        uint16_t *nextCluster) {
  if (diskFileCount >= DISK_MAX_FILES) {
    return false;
  }
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  uint32_t size = file.size();
  uint16_t clusters = (size + DISK_CLUSTER_BYTES - 1) / DISK_CLUSTER_BYTES;
  if (clusters == 0) {
    clusters = 1;
  }
  uint16_t maxCluster = (DISK_SECTORS - DISK_DATA_START_SECTOR) /
                        DISK_SECTORS_PER_CLUSTER + 1;
  if ((uint32_t)*nextCluster + clusters - 1 > maxCluster) {
    file.close();
    return false;
  }

  uint16_t firstCluster = *nextCluster;
  uint32_t remaining = size;
  for (uint16_t i = 0; i < clusters; i++) {
    uint16_t cluster = firstCluster + i;
    uint16_t next = (i + 1 == clusters) ? 0xFFF : cluster + 1;
    fat12Set(fat, cluster, next);
    uint32_t offset = (DISK_DATA_START_SECTOR +
                       (uint32_t)(cluster - 2) * DISK_SECTORS_PER_CLUSTER) * 512;
    uint32_t count = remaining < DISK_CLUSTER_BYTES
                       ? remaining : DISK_CLUSTER_BYTES;
    if (count > 0 && file.read(disk + offset, count) != count) {
      file.close();
      return false;
    }
    remaining -= count;
  }
  file.close();

  DiskFileEntry &entry = diskFiles[diskFileCount];
  copyField(entry.path, sizeof(entry.path), path);
  memcpy(entry.shortName, shortName, sizeof(entry.shortName));
  entry.size = size;
  entry.firstCluster = firstCluster;
  *nextCluster += clusters;

  uint8_t *directoryEntry = root + diskFileCount * 32;
  memcpy(directoryEntry, shortName, 11);
  directoryEntry[11] = 0x20;
  directoryEntry[26] = firstCluster & 0xFF;
  directoryEntry[27] = (firstCluster >> 8) & 0xFF;
  directoryEntry[28] = size & 0xFF;
  directoryEntry[29] = (size >> 8) & 0xFF;
  directoryEntry[30] = (size >> 16) & 0xFF;
  directoryEntry[31] = (size >> 24) & 0xFF;
  diskFileCount++;
  return true;
}

static bool isDrawingSvg(const char *path) {
  if (path == nullptr || strncmp(path, "/draw-", 6) != 0) {
    return false;
  }
  size_t length = strlen(path);
  return length > 4 && strcmp(path + length - 4, ".svg") == 0;
}

static bool refreshDisk() {
  if (disk == nullptr || !storageOk) {
    return false;
  }
  memset(disk, 0, DISK_BYTES);
  diskFileCount = 0;
  uint8_t *boot = disk;
  boot[0] = 0xEB;
  boot[1] = 0x3C;
  boot[2] = 0x90;
  memcpy(boot + 3, "MSDOS5.0", 8);
  boot[11] = 0x00;
  boot[12] = 0x02;
  boot[13] = DISK_SECTORS_PER_CLUSTER;
  boot[14] = 1;
  boot[16] = 1;
  boot[17] = DISK_ROOT_ENTRIES & 0xFF;
  boot[18] = (DISK_ROOT_ENTRIES >> 8) & 0xFF;
  boot[19] = DISK_SECTORS & 0xFF;
  boot[20] = (DISK_SECTORS >> 8) & 0xFF;
  boot[21] = 0xF8;
  boot[22] = DISK_FAT_SECTORS;
  boot[24] = 1;
  boot[26] = 1;
  boot[38] = 0x29;
  boot[39] = 0x34;
  boot[40] = 0x12;
  memcpy(boot + 43, "CLEAR WEEK ", 11);
  memcpy(boot + 54, "FAT12   ", 8);
  boot[510] = 0x55;
  boot[511] = 0xAA;

  uint8_t *fat = disk + 512;
  uint8_t *dir = disk + (1 + DISK_FAT_SECTORS) * 512;
  fat12Set(fat, 0, 0xFF8);
  fat12Set(fat, 1, 0xFFF);
  uint16_t nextCluster = 2;
  static const char logName[11] = {
    'C', 'L', 'E', 'A', 'R', ' ', ' ', ' ', 'C', 'S', 'V'
  };
  if (!addDiskFile(LOG_PATH, logName, fat, dir, &nextCluster)) {
    return false;
  }

  File root = SPIFFS.open("/");
  if (!root) {
    return false;
  }
  uint16_t drawingNumber = 1;
  while (File file = root.openNextFile()) {
    const char *path = file.name();
    bool drawing = isDrawingSvg(path);
    char drawingPath[48];
    copyField(drawingPath, sizeof(drawingPath), path);
    file.close();
    if (!drawing) {
      continue;
    }
    char shortName[11];
    memset(shortName, ' ', sizeof(shortName));
    char baseName[9];
    snprintf(baseName, sizeof(baseName), "DRAW%04u", drawingNumber++);
    memcpy(shortName, baseName, 8);
    memcpy(shortName + 8, "SVG", 3);
    if (!addDiskFile(drawingPath, shortName, fat, dir, &nextCluster)) {
      root.close();
      return false;
    }
  }
  root.close();
  return true;
}

static bool diskHasLog() {
  if (disk == nullptr) {
    return false;
  }
  uint8_t *dir = disk + (1 + DISK_FAT_SECTORS) * 512;
  for (int i = 0; i < DISK_ROOT_ENTRIES; i++) {
    uint8_t *entry = dir + i * 32;
    if (entry[0] == 0x00) {
      break;
    }
    if (entry[0] == 0xE5) {
      continue;
    }
    if (memcmp(entry, "CLEAR   ", 8) == 0 && memcmp(entry + 8, "CSV", 3) == 0) {
      uint32_t size = (uint32_t)entry[28] | ((uint32_t)entry[29] << 8) |
                      ((uint32_t)entry[30] << 16) | ((uint32_t)entry[31] << 24);
      return size > 0;
    }
  }
  return false;
}

static void clearWeekFromComputer() {
  writeFreshLog();
  waitingCount = 0;
  viewIndex = 0;
  refreshDisk();
  paintScreen();
}

#if CLEAR_USB_DRIVE
static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (disk == nullptr || lba >= DISK_SECTORS) {
    return 0;
  }
  memcpy(buffer, disk + (lba * 512) + offset, bufsize);
  return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (disk == nullptr || lba >= DISK_SECTORS) {
    return 0;
  }
  memcpy(disk + (lba * 512) + offset, buffer, bufsize);
  return bufsize;
}

static bool onStartStop(uint8_t power, bool start, bool eject) {
  (void)power;
  (void)eject;
  if (!start) {
    usbMounted = false;
    if (!diskHasLog()) {
      clearWeekFromComputer();
    }
  }
  return true;
}

static void usbEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)data;
  if (base != ARDUINO_USB_EVENTS) {
    return;
  }
  if (id == ARDUINO_USB_STARTED_EVENT) {
    refreshDisk();
    usbMounted = true;
  } else if (id == ARDUINO_USB_STOPPED_EVENT) {
    usbMounted = false;
    if (!diskHasLog()) {
      clearWeekFromComputer();
    }
  }
}

static void startUsbDrive() {
  disk = (uint8_t *)heap_caps_malloc(DISK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (disk == nullptr) {
    disk = (uint8_t *)malloc(DISK_BYTES);
  }
  if (disk == nullptr) {
    return;
  }
  refreshDisk();
  USB.onEvent(usbEvent);
  MSC.vendorID("CLEAR");
  MSC.productID("CLEAR week");
  MSC.productRevision("1.0");
  MSC.onStartStop(onStartStop);
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.isWritable(true);
  MSC.begin(DISK_SECTORS, 512);
  USB.begin();
}
#else
static void startUsbDrive() {}
#endif

static void remember(const ClearNote *note) {
  for (int i = 0; i < waitingCount; i++) {
    if (waiting[i].eventId == note->eventId &&
        strcmp(waiting[i].student, note->student) == 0) {
      waiting[i] = *note;
      return;
    }
  }
  if (waitingCount >= LIST_MAX) {
    for (int i = 1; i < LIST_MAX; i++) {
      waiting[i - 1] = waiting[i];
    }
    waitingCount = LIST_MAX - 1;
    if (viewIndex > 0) {
      viewIndex--;
    }
  }
  waiting[waitingCount] = *note;
  waitingCount++;
}

static void takeNotice(const uint8_t *data, int len) {
  if (len == (int)sizeof(ClearOcrTextAck)) {
    ClearOcrTextAck ack;
    memcpy(&ack, data, sizeof(ack));
    if (ack.version != CLEAR_LINK_VERSION ||
        ack.kind != CLEAR_KIND_OCR_TEXT_ACK || ack.accepted > 1) {
      return;
    }
    portENTER_CRITICAL(&ocrAckMux);
    ocrAckDrawingId = ack.drawingId;
    ocrAckChunkIndex = ack.chunkIndex;
    ocrAckAccepted = ack.accepted != 0;
    ocrAckReady = true;
    portEXIT_CRITICAL(&ocrAckMux);
    return;
  }
  if (len >= 2 && data[0] == CLEAR_LINK_VERSION &&
      data[1] >= CLEAR_KIND_DRAWING_START &&
      data[1] <= CLEAR_KIND_DRAWING_END &&
      len <= (int)sizeof(ClearDrawingChunk)) {
    uint8_t next = (drawingRingWrite + 1) % DRAWING_RING_MAX;
    if (next == drawingRingRead) {
      if (len >= (int)offsetof(ClearDrawingStart, student)) {
        uint32_t overflowId = 0;
        memcpy(&overflowId, data + offsetof(ClearDrawingStart, drawingId),
               sizeof(overflowId));
        drawingOverflowId = overflowId;
      }
      drawingQueueOverflow = true;
      return;
    }
    memcpy(drawingRing[drawingRingWrite].bytes, data, len);
    drawingRing[drawingRingWrite].length = len;
    drawingRingWrite = next;
    return;
  }
  if (len != (int)sizeof(ClearNote)) {
    return;
  }
  ClearNote note;
  memcpy(&note, data, sizeof(note));
  note.student[sizeof(note.student) - 1] = 0;
  note.feeling[sizeof(note.feeling) - 1] = 0;
  note.choice[sizeof(note.choice) - 1] = 0;
  if (note.version != CLEAR_LINK_VERSION || note.kind != CLEAR_KIND_NOTICE) {
    return;
  }
  uint8_t next = (ringWrite + 1) % RING_MAX;
  if (next == ringRead) {
    return;
  }
  noticeRing[ringWrite] = note;
  ringWrite = next;
}

class NoticeCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    size_t length = characteristic->getValue().length();
    if (length > sizeof(ClearDrawingChunk)) {
      return;
    }
    takeNotice(characteristic->getData(), length);
  }
};

static NoticeCallbacks noticeCallbacks;

static void startRadio() {
  BLEDevice::init(CLEAR_BLE_NAME);
  BLEDevice::setMTU(CLEAR_BLE_MTU);
  // Secure Connections with a passkey. Both panels use the class PIN,
  // and the bond is remembered for the next class.
  BLEDevice::setSecurityCallbacks(new ClearSecurityCallbacks());
  BLESecurity::setCapability(ESP_IO_CAP_OUT);
  BLESecurity::setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  BLESecurity::setKeySize(16);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setPassKey(true, classPasskey());
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(CLEAR_SERVICE_UUID);
  BLECharacteristic *notice = service->createCharacteristic(
    CLEAR_NOTICE_UUID, BLECharacteristic::PROPERTY_WRITE);
  notice->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  notice->setCallbacks(&noticeCallbacks);
  replyChar = service->createCharacteristic(
    CLEAR_REPLY_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  replyChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  replyChar->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(CLEAR_SERVICE_UUID);
  advertising->start();
}

static void sendDrawingResult(uint32_t drawingId, bool saved) {
  if (replyChar == nullptr) {
    return;
  }
  ClearDrawingResult result = {};
  result.version = CLEAR_LINK_VERSION;
  result.kind = CLEAR_KIND_DRAWING_RESULT;
  result.drawingId = drawingId;
  result.saved = saved ? 1 : 0;
  replyChar->setValue((uint8_t *)&result, sizeof(result));
  replyChar->notify();
}

static void closeActiveDrawing(bool removePartial) {
  if (activeDrawingFile) {
    activeDrawingFile.close();
  }
  if (removePartial && activeDrawingTempPath[0]) {
    SPIFFS.remove(activeDrawingTempPath);
  }
  activeDrawingId = 0;
  activeDrawingBytes = 0;
  activeDrawingReceived = 0;
  activeDrawingChunkIndex = 0;
  activeDrawingTempPath[0] = 0;
  activeDrawingPath[0] = 0;
}

// Keep a limited set of drawing files. Older pictures are removed so
// student work does not fill this device.
static void pruneOldDrawings(const char *keepPath) {
  for (int guard = 0; guard < 8; guard++) {
    int count = 0;
    char oldestPath[48];
    oldestPath[0] = 0;
    time_t oldest = 0;
    bool haveOldest = false;
    File root = SPIFFS.open("/");
    if (!root) {
      return;
    }
    while (File file = root.openNextFile()) {
      const char *name = file.name();
      char path[48];
      if (name != nullptr && name[0] == '/') {
        copyField(path, sizeof(path), name);
      } else {
        snprintf(path, sizeof(path), "/%s", name == nullptr ? "" : name);
      }
      bool drawing = isDrawingSvg(path);
      time_t written = file.getLastWrite();
      file.close();
      if (!drawing) {
        continue;
      }
      count++;
      if (keepPath != nullptr && strcmp(path, keepPath) == 0) {
        continue;
      }
      if (!haveOldest || written < oldest) {
        haveOldest = true;
        oldest = written;
        copyField(oldestPath, sizeof(oldestPath), path);
      }
    }
    root.close();
    size_t freeBytes = SPIFFS.totalBytes() > SPIFFS.usedBytes()
                         ? SPIFFS.totalBytes() - SPIFFS.usedBytes() : 0;
    if (count <= 16 && freeBytes > 80000) {
      return;
    }
    if (!haveOldest) {
      return;
    }
    SPIFFS.remove(oldestPath);
  }
}

static void beginDrawingTransfer(const ClearDrawingStart *packet) {
  closeActiveDrawing(true);
  if (!storageOk || packet->board > 2 || packet->totalBytes == 0 ||
      packet->totalBytes > DRAWING_MAX_BYTES) {
    sendDrawingResult(packet->drawingId, false);
    return;
  }
  snprintf(activeDrawingPath, sizeof(activeDrawingPath),
           "/draw-%08lX.svg", (unsigned long)packet->drawingId);
  snprintf(activeDrawingTempPath, sizeof(activeDrawingTempPath),
           "/draw-%08lX.part", (unsigned long)packet->drawingId);
  SPIFFS.remove(activeDrawingTempPath);
  activeDrawingFile = SPIFFS.open(activeDrawingTempPath, FILE_WRITE);
  if (!activeDrawingFile) {
    closeActiveDrawing(false);
    sendDrawingResult(packet->drawingId, false);
    return;
  }
  activeDrawingId = packet->drawingId;
  activeDrawingBoard = packet->board;
  activeDrawingBytes = packet->totalBytes;
  activeDrawingReceived = 0;
  activeDrawingChunkIndex = 0;
}

static void handleDrawingPacket(const uint8_t *data, uint8_t length) {
  if (length < 2 || data[0] != CLEAR_LINK_VERSION) {
    return;
  }
  if (data[1] == CLEAR_KIND_DRAWING_START &&
      length == sizeof(ClearDrawingStart)) {
    ClearDrawingStart packet;
    memcpy(&packet, data, sizeof(packet));
    beginDrawingTransfer(&packet);
    return;
  }
  if (data[1] == CLEAR_KIND_DRAWING_CHUNK &&
      length >= offsetof(ClearDrawingChunk, payload)) {
    ClearDrawingChunk packet = {};
    memcpy(&packet, data, length);
    if (activeDrawingId != packet.drawingId ||
        packet.chunkIndex != activeDrawingChunkIndex ||
        packet.payloadSize > CLEAR_DRAWING_CHUNK_SIZE ||
        length != offsetof(ClearDrawingChunk, payload) + packet.payloadSize ||
        activeDrawingReceived + packet.payloadSize > activeDrawingBytes) {
      closeActiveDrawing(true);
      sendDrawingResult(packet.drawingId, false);
      return;
    }
    size_t written = activeDrawingFile.write(packet.payload, packet.payloadSize);
    if (written != packet.payloadSize) {
      uint32_t failedId = activeDrawingId;
      closeActiveDrawing(true);
      sendDrawingResult(failedId, false);
      return;
    }
    activeDrawingReceived += written;
    activeDrawingChunkIndex++;
    return;
  }
  if (data[1] == CLEAR_KIND_DRAWING_END &&
      length == sizeof(ClearDrawingEnd)) {
    ClearDrawingEnd packet;
    memcpy(&packet, data, sizeof(packet));
    bool complete = activeDrawingId == packet.drawingId &&
                    activeDrawingReceived == activeDrawingBytes;
    uint32_t finishedId = packet.drawingId;
    uint8_t finishedBoard = activeDrawingBoard;
    char finishedPath[sizeof(activeDrawingPath)];
    copyField(finishedPath, sizeof(finishedPath), activeDrawingPath);
    if (activeDrawingFile) {
      activeDrawingFile.flush();
      activeDrawingFile.close();
    }
    char backupPath[40];
    snprintf(backupPath, sizeof(backupPath),
             "/draw-%08lX.bak", (unsigned long)finishedId);
    bool hadPrevious = complete && SPIFFS.exists(activeDrawingPath);
    if (hadPrevious) {
      SPIFFS.remove(backupPath);
      complete = SPIFFS.rename(activeDrawingPath, backupPath);
    }
    if (complete) {
      complete = SPIFFS.rename(activeDrawingTempPath, activeDrawingPath);
    }
    if (!complete) {
      SPIFFS.remove(activeDrawingTempPath);
      if (hadPrevious) {
        SPIFFS.rename(backupPath, activeDrawingPath);
      }
    } else if (hadPrevious) {
      SPIFFS.remove(backupPath);
    }
    closeActiveDrawing(false);
    sendDrawingResult(finishedId, complete);
    if (complete) {
      pruneOldDrawings(finishedPath);
      uint8_t next = (ocrJobWrite + 1) % OCR_QUEUE_MAX;
      if (next == ocrJobRead) {
        ocrJobOverflow = true;
      } else {
        OcrJob &job = ocrJobs[ocrJobWrite];
        memset(&job, 0, sizeof(job));
        copyField(job.path, sizeof(job.path), finishedPath);
        job.drawingId = finishedId;
        job.board = finishedBoard;
        ocrJobWrite = next;
      }
    }
  }
}

static bool clearOcrConfigured() {
  return CLEAR_WIFI_SSID[0] != 0 && CLEAR_OCR_SERVER_URL[0] != 0 &&
         CLEAR_OCR_TOKEN[0] != 0;
}

static void startOcrWifi() {
  if (!clearOcrConfigured()) {
    Serial.println("Local OCR is disabled. Configure CLEAR_Teacher_Config.h.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(CLEAR_WIFI_SSID, CLEAR_WIFI_PASSWORD);
  nextOcrWifiRetryMs = millis() + 30000;
  Serial.println("Connecting teacher to the local OCR Wi-Fi network.");
}

static bool sendOcrPacketAndWait(const uint8_t *data, size_t length,
                                 uint32_t drawingId, uint16_t chunkIndex) {
  if (replyChar == nullptr) return false;
  for (int attempt = 0; attempt < 3; attempt++) {
    portENTER_CRITICAL(&ocrAckMux);
    ocrAckReady = false;
    portEXIT_CRITICAL(&ocrAckMux);
    replyChar->setValue((uint8_t *)data, length);
    replyChar->notify();
    unsigned long started = millis();
    while ((unsigned long)(millis() - started) < 1800) {
      bool ready;
      uint32_t ackId;
      uint16_t ackIndex;
      bool accepted;
      portENTER_CRITICAL(&ocrAckMux);
      ready = ocrAckReady;
      ackId = ocrAckDrawingId;
      ackIndex = ocrAckChunkIndex;
      accepted = ocrAckAccepted;
      if (ready) ocrAckReady = false;
      portEXIT_CRITICAL(&ocrAckMux);
      if (ready && ackId == drawingId && ackIndex == chunkIndex) {
        return accepted;
      }
      delay(2);
    }
  }
  return false;
}

static bool sendOcrResultToPanel(OcrJob &job) {
  if (job.textSize == 0 || job.textSize > 600) return false;
  ClearOcrTextStart start = {};
  start.version = CLEAR_LINK_VERSION;
  start.kind = CLEAR_KIND_OCR_TEXT_START;
  start.drawingId = job.drawingId;
  start.board = job.board;
  start.totalBytes = job.textSize;
  if (!sendOcrPacketAndWait((const uint8_t *)&start, sizeof(start),
                            job.drawingId, 0xFFFF)) return false;

  uint16_t chunkCount = 0;
  size_t offset = 0;
  while (offset < job.textSize) {
    ClearOcrTextChunk chunk = {};
    chunk.version = CLEAR_LINK_VERSION;
    chunk.kind = CLEAR_KIND_OCR_TEXT_CHUNK;
    chunk.drawingId = job.drawingId;
    chunk.chunkIndex = chunkCount;
    size_t count = job.textSize - offset;
    if (count > CLEAR_DRAWING_CHUNK_SIZE) count = CLEAR_DRAWING_CHUNK_SIZE;
    chunk.payloadSize = count;
    memcpy(chunk.payload, job.text + offset, count);
    size_t packetSize = offsetof(ClearOcrTextChunk, payload) + count;
    if (!sendOcrPacketAndWait((const uint8_t *)&chunk, packetSize,
                              job.drawingId, chunkCount)) return false;
    offset += count;
    chunkCount++;
  }

  ClearOcrTextEnd end = {};
  end.version = CLEAR_LINK_VERSION;
  end.kind = CLEAR_KIND_OCR_TEXT_END;
  end.drawingId = job.drawingId;
  end.chunkCount = chunkCount;
  return sendOcrPacketAndWait((const uint8_t *)&end, sizeof(end),
                              job.drawingId, 0xFFFE);
}

static OcrProcessResult recognizeDrawingLocally(OcrJob &job) {
  if (!job.serverJobStarted) {
    File svg = SPIFFS.open(job.path, FILE_READ);
    if (!svg || svg.size() == 0 || svg.size() > DRAWING_MAX_BYTES) {
      if (svg) svg.close();
      Serial.printf("Local OCR: drawing %08lX is missing or too large.\n",
                    (unsigned long)job.drawingId);
      return OCR_PROCESS_FAILED;
    }

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, CLEAR_OCR_SERVER_URL "/recognize")) {
      svg.close();
      Serial.println("Local OCR server URL is invalid.");
      return OCR_PROCESS_RETRY;
    }
    http.addHeader("Content-Type", "image/svg+xml");
    http.addHeader("X-CLEAR-Token", CLEAR_OCR_TOKEN);
    Serial.printf("Submitting drawing %08lX to local OCR.\n",
                  (unsigned long)job.drawingId);
    int responseCode = http.sendRequest("POST", &svg, svg.size());
    svg.close();
    if (responseCode != HTTP_CODE_ACCEPTED) {
      http.end();
      Serial.printf("Local OCR submission failed (HTTP %d).\n", responseCode);
      return OCR_PROCESS_RETRY;
    }

    String jobId = http.getString();
    http.end();
    jobId.trim();
    if (jobId.length() == 0 || jobId.length() >= sizeof(job.remoteJobId)) {
      Serial.println("Local OCR server returned an invalid job ID.");
      return OCR_PROCESS_RETRY;
    }
    copyField(job.remoteJobId, sizeof(job.remoteJobId), jobId.c_str());
    job.serverJobStarted = true;
    job.remoteJobStartedMs = millis();
    job.nextRemotePollMs = millis() + 1000;
    return OCR_PROCESS_WAITING;
  }

  if ((long)(millis() - job.nextRemotePollMs) < 0) {
    return OCR_PROCESS_WAITING;
  }
  if ((unsigned long)(millis() - job.remoteJobStartedMs) >= 300000) {
    job.serverJobStarted = false;
    job.remoteJobId[0] = 0;
    Serial.printf("Local OCR timed out for drawing %08lX.\n",
                  (unsigned long)job.drawingId);
    return OCR_PROCESS_RETRY;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  String resultUrl = String(CLEAR_OCR_SERVER_URL) + "/result/" +
                     job.remoteJobId;
  if (!http.begin(client, resultUrl)) {
    Serial.println("Local OCR result URL is invalid.");
    job.nextRemotePollMs = millis() + 5000;
    return OCR_PROCESS_WAITING;
  }
  http.addHeader("X-CLEAR-Token", CLEAR_OCR_TOKEN);
  int responseCode = http.GET();
  if (responseCode == 202) {
    http.end();
    job.nextRemotePollMs = millis() + 1000;
    return OCR_PROCESS_WAITING;
  }
  if (responseCode == HTTP_CODE_OK) {
    String text = http.getString();
    http.end();
    WiFiClient cleanupClient;
    HTTPClient cleanup;
    if (cleanup.begin(cleanupClient, resultUrl)) {
      cleanup.addHeader("X-CLEAR-Token", CLEAR_OCR_TOKEN);
      cleanup.sendRequest("DELETE");
      cleanup.end();
    }
    if (text.length() == 0 || text.length() > 600) {
      Serial.println("Local OCR returned no text or text exceeded 600 bytes.");
      return OCR_PROCESS_FAILED;
    }
    for (size_t i = 0; i < text.length(); i++) {
      if (text[i] == 0 || (uint8_t)text[i] < 0x09 ||
          ((uint8_t)text[i] > 0x0D && (uint8_t)text[i] < 0x20)) {
        Serial.println("Local OCR returned unsupported control characters.");
        return OCR_PROCESS_FAILED;
      }
    }
    memcpy(job.text, text.c_str(), text.length());
    job.text[text.length()] = 0;
    job.textSize = text.length();
    job.textReady = true;
    job.serverJobStarted = false;
    job.remoteJobId[0] = 0;
    return OCR_PROCESS_READY;
  }
  http.end();
  if (responseCode == 404 || responseCode == 422) {
    Serial.printf("Local OCR could not recognize drawing %08lX (HTTP %d).\n",
                  (unsigned long)job.drawingId, responseCode);
    job.serverJobStarted = false;
    job.remoteJobId[0] = 0;
    return OCR_PROCESS_FAILED;
  }
  Serial.printf("Could not check local OCR job (HTTP %d).\n", responseCode);
  job.nextRemotePollMs = millis() + 5000;
  return OCR_PROCESS_WAITING;
}

static void serviceOcrJobs() {
  if (ocrJobOverflow) {
    ocrJobOverflow = false;
    Serial.println("Local OCR queue is full; a saved drawing was not queued.");
  }
  if (ocrJobRead == ocrJobWrite) return;
  if (!clearOcrConfigured()) {
    if (!ocrConfigWarningShown) {
      Serial.println("Drawings are saved, but local OCR is not configured.");
      ocrConfigWarningShown = true;
    }
    return;
  }
  ocrConfigWarningShown = false;
  if (WiFi.status() != WL_CONNECTED) {
    if ((long)(millis() - nextOcrWifiRetryMs) >= 0) {
      WiFi.disconnect(false, false);
      WiFi.begin(CLEAR_WIFI_SSID, CLEAR_WIFI_PASSWORD);
      nextOcrWifiRetryMs = millis() + 30000;
      Serial.println("Waiting for the local OCR Wi-Fi connection.");
    }
    return;
  }
  serviceNetworkClock();
  if ((long)(millis() - nextOcrAttemptMs) < 0) return;

  OcrJob &job = ocrJobs[ocrJobRead];
  if (!job.textReady) {
    OcrProcessResult result = recognizeDrawingLocally(job);
    if (result == OCR_PROCESS_WAITING) return;
    if (result == OCR_PROCESS_RETRY) {
      nextOcrAttemptMs = millis() + 15000;
      return;
    }
    if (result == OCR_PROCESS_FAILED) {
      // The drawing is already stored. Tell the panel that reading
      // stopped, and leave the picture itself alone.
      const char *miss = "Reading did not finish. The drawing is still saved.";
      copyField(job.text, sizeof(job.text), miss);
      job.textSize = strlen(job.text);
      job.textReady = true;
    }
  }

  if (!sendOcrResultToPanel(job)) {
    Serial.printf("OCR text for drawing %08lX is waiting for BLE return.\n",
                  (unsigned long)job.drawingId);
    nextOcrAttemptMs = millis() + 5000;
    return;
  }
  Serial.printf("OCR text for drawing %08lX was saved on the student panel.\n",
                (unsigned long)job.drawingId);
  ocrJobRead = (ocrJobRead + 1) % OCR_QUEUE_MAX;
  nextOcrAttemptMs = 0;
}

static void drainDrawingPackets() {
  if (drawingQueueOverflow) {
    noInterrupts();
    uint32_t overflowId = drawingOverflowId;
    drawingOverflowId = 0;
    drawingQueueOverflow = false;
    interrupts();
    if (overflowId != 0) {
      sendDrawingResult(overflowId, false);
    }
  }
  while (drawingRingRead != drawingRingWrite) {
    DrawingPacketEntry packet = drawingRing[drawingRingRead];
    drawingRingRead = (drawingRingRead + 1) % DRAWING_RING_MAX;
    handleDrawingPacket(packet.bytes, packet.length);
  }
}

static void notifyReply(ClearNote note, uint8_t answer) {
  note.kind = CLEAR_KIND_REPLY;
  note.answer = answer;
  uint32_t now = clockNow();
  if (now > note.timeSent) {
    note.timeSent = now;
  }
  if (replyChar != nullptr) {
    replyChar->setValue((uint8_t *)&note, sizeof(note));
    replyChar->notify();
  }
}

static void sendReply(ClearNote note, uint8_t answer) {
  notifyReply(note, answer);
  note.kind = CLEAR_KIND_REPLY;
  note.answer = answer;
  appendRow(&note, clockNow());
}

static void answerCurrent(uint8_t answer) {
  if (waitingCount < 1 || viewIndex < 0 || viewIndex >= waitingCount) {
    return;
  }
  ClearNote note = waiting[viewIndex];
  for (int i = viewIndex + 1; i < waitingCount; i++) {
    waiting[i - 1] = waiting[i];
  }
  waitingCount--;
  viewIndex = 0;
  sendReply(note, answer);
  paintScreen();
}

static void cycleStudent() {
  if (waitingCount < 2) {
    return;
  }
  viewIndex = (viewIndex + 1) % waitingCount;
  paintScreen();
}

// One press counts once. A finger resting on Yes must not answer again.
static bool takeTap(int32_t *x, int32_t *y) {
  int32_t tx = 0;
  int32_t ty = 0;
  bool down = lcd.getTouch(&tx, &ty);
  bool hit = down && !touchWasDown && (millis() - buttonMs) >= BUTTON_GAP_MS;
  if (hit) {
    buttonMs = millis();
    *x = tx;
    *y = ty;
  }
  touchWasDown = down;
  return hit;
}

static void drainNotices() {
  while (ringRead != ringWrite) {
    ClearNote note = noticeRing[ringRead];
    ringRead = (ringRead + 1) % RING_MAX;
    if (note.asksTeacher == CLEAR_ASK_HELPED || note.asksTeacher == CLEAR_ASK_NOTYET) {
      if (nameIsTrusted(note.student)) {
        adoptStudentClock(note.timeSent);
        note.answer = CLEAR_ANSWER_NONE;
        appendRow(&note, 0);
      }
      continue;
    }
    buzzFor(&note);
    if (!nameIsTrusted(note.student)) {
      holdPending(&note);
      notifyReply(note, CLEAR_ANSWER_PENDING);
      paintScreen();
      continue;
    }
    adoptStudentClock(note.timeSent);
    note.answer = CLEAR_ANSWER_NONE;
    appendRow(&note, 0);
    note.answer = CLEAR_ANSWER_GOT;
    remember(&note);
    reportStatus(note, CLEAR_ANSWER_GOT);
    paintScreen();
  }
  drainDrawingPackets();
}

static void finishSignIn() {
  screenMode = MODE_QUEUE;
  activityMs = millis();
  if (storageOk && loadWeekPlain()) {
    normalizeWeekPlain();
    saveWeekPlain();
  }
  if (!servicesStarted) {
    startRadio();
    startUsbDrive();
    startOcrWifi();
    servicesStarted = true;
  }
  paintScreen();
}

void setup() {
  Serial.begin(115200);
  setenv("TZ", CLOCK_TZ, 1);
  tzset();
  pinMode(PIN_BLANK, INPUT_PULLUP);
  pinMode(PIN_BUZZ, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZ, 2000, 8);
  ledcWrite(PIN_BUZZ, 0);
#else
  ledcSetup(7, 2000, 8);
  ledcAttachPin(PIN_BUZZ, 7);
  ledcWrite(7, 0);
#endif
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  loadPrivacy();
  lcd.init();
  lcd.setRotation(0);
  applyBrightness();
  startStorage();
  startTeacherClock();
  activityMs = millis();
  paintScreen();
}

void loop() {
  serviceBlank();
  serviceBuzz();
  if (screenBlank) {
    if (servicesStarted) {
      drainNotices();
      serviceOcrJobs();
    }
    return;
  }
  if (servicesStarted) {
    drainNotices();
    serviceOcrJobs();
    serviceNetworkClock();
    if ((long)(millis() - clockSaveMs) >= 60000) {
      saveTeacherClock();
      clockSaveMs = millis();
    }
  }
  int32_t tx = 0;
  int32_t ty = 0;
  if (!takeTap(&tx, &ty)) {
    if (screenMode == MODE_QUEUE && (millis() - activityMs) >= PRIVACY_IDLE_MS) {
      screenMode = MODE_COVER;
      clearEntry();
      copyField(privacyNote, sizeof(privacyNote), "Enter the class PIN");
      paintScreen();
    }
    return;
  }
  activityMs = millis();
  if (screenMode == MODE_LOOK) {
    if (tx >= 148 && ty < 52) {
      screenMode = lookReturn;
    } else if (ty >= 80 && ty < 140) {
      if (tx < 52) {
        setBright(teacherBright - 10);
      } else if (tx >= 188) {
        setBright(teacherBright + 10);
      } else if (tx >= 52 && tx < 188) {
        int rel = tx - 52;
        if (rel < 0) {
          rel = 0;
        }
        if (rel > 136) {
          rel = 136;
        }
        setBright(BRIGHT_MIN + rel * (BRIGHT_MAX - BRIGHT_MIN) / 136);
      }
    } else if (ty >= 176 && ty < 220) {
      bool nextDark = tx >= 124;
      if (nextDark != teacherDark) {
        teacherDark = nextDark;
        applyLook();
        saveLook();
      }
    }
    paintScreen();
    return;
  }
  if (gearHit(tx, ty)) {
    lookReturn = screenMode;
    screenMode = MODE_LOOK;
    paintScreen();
    return;
  }
  if (screenMode == MODE_PENDING) {
    if (ty < BUTTON_Y) {
      return;
    }
    if (pendingCount < 1) {
      screenMode = MODE_QUEUE;
      paintScreen();
      return;
    }
    if (tx < NO_X) {
      ClearNote note = pendingNotes[pendingView];
      if (!nameIsTrusted(note.student) && trustedCount < TRUSTED_MAX) {
        copyField(trustedNames[trustedCount], sizeof(trustedNames[0]), note.student);
        trustedCount++;
        saveTrusted();
      }
      for (int i = pendingView + 1; i < pendingCount; i++) {
        pendingNotes[i - 1] = pendingNotes[i];
      }
      pendingCount--;
      if (pendingView >= pendingCount) {
        pendingView = 0;
      }
      note.answer = CLEAR_ANSWER_NONE;
      adoptStudentClock(note.timeSent);
      appendRow(&note, 0);
      remember(&note);
      if (pendingCount == 0) {
        screenMode = MODE_QUEUE;
      }
    } else if (tx < NEXT_X) {
      screenMode = MODE_QUEUE;
    } else if (pendingCount > 1) {
      pendingView = (pendingView + 1) % pendingCount;
    }
    paintScreen();
    return;
  }
  if (screenMode == MODE_TRUSTED) {
    if (ty < BUTTON_Y) {
      return;
    }
    if (tx < NO_X && trustedCount > 0) {
      for (int i = trustedView + 1; i < trustedCount; i++) {
        memcpy(trustedNames[i - 1], trustedNames[i], sizeof(trustedNames[0]));
      }
      trustedCount--;
      if (trustedView >= trustedCount) {
        trustedView = 0;
      }
      saveTrusted();
    } else if (tx < NEXT_X) {
      screenMode = MODE_QUEUE;
    } else if (trustedCount > 1) {
      trustedView = (trustedView + 1) % trustedCount;
    }
    paintScreen();
    return;
  }
  if (screenMode != MODE_QUEUE) {
    char key = 0;
    if (privacyKeyAt(tx, ty, &key)) {
      typePrivacyKey(key);
      paintScreen();
    }
    return;
  }
  if (ty >= 236 && ty < BUTTON_Y && pendingCount > 0) {
    screenMode = MODE_PENDING;
    paintScreen();
    return;
  }
  if (ty < 90 && waitingCount < 1) {
    screenMode = MODE_TRUSTED;
    paintScreen();
    return;
  }
  if (ty >= BUTTON_Y) {
    if (tx < NO_X) {
      answerCurrent(CLEAR_ANSWER_YES);
    } else if (tx < NEXT_X) {
      answerCurrent(CLEAR_ANSWER_NO);
    } else if (viewingAid()) {
      answerCurrent(CLEAR_ANSWER_LATER);
    } else {
      cycleStudent();
    }
  }
}
