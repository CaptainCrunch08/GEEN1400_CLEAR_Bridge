/*
  GEEN1400 teacher device
  Seeed XIAO ESP32-S3 Sense, Waveshare 2.4 inch LCD (240x320, ILI9341, SPI),
  and three buttons.

  Board in the Arduino IDE: XIAO_ESP32S3
  PSRAM: OPI PSRAM
  USB CDC On Boot: Enabled
  USB Mode: USB-OTG (TinyUSB). That is the board default. Hardware CDC
  mode cannot show the week as a USB drive.
  Partition: Default with spiffs

  Wiring. The LCD clock is assigned to GPIO8 as requested. GPIO8 is also
  connected to the Sense microSD socket's clock line. Tie LCD RST to 3V3.

  LCD VCC  3V3
  LCD GND  GND
  LCD DIN  D5   GPIO6
  LCD CLK  GPIO8
  LCD CS   D3   GPIO4
  LCD DC   GPIO7
  LCD RST  3V3
  LCD BL   D0   GPIO1

  Yes button   D1  GPIO2    other side to GND
  No button    D6  GPIO43   other side to GND
  Next button  D7  GPIO44   other side to GND

  A student panel sends the name, the issue, and the request or solution
  over Bluetooth. This screen opens on the oldest one. Next moves to the
  following student. Yes and No answer the student on screen and send
  that answer back.

  When CLEAR_Teacher_Config.h is filled in, completed drawings are sent
  over Wi-Fi to the local OCR computer and recognized text returns over BLE.

  The week is kept in the chip's flash. Plugging the USB-C port into a
  computer shows a drive with CLEAR.CSV. Copying the file leaves the
  week on the device. Deleting CLEAR.CSV and unplugging starts a new
  empty week.

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
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <esp_heap_caps.h>
#include "CLEAR_Link.h"
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
static const int PIN_LCD_SCK = 8;
static const int PIN_LCD_CS = 4;
static const int PIN_LCD_DC = 7;
static const int PIN_LCD_BL = 1;
static const int PIN_SD_CS = 21;
static const int PIN_YES = 2;
static const int PIN_NO = 43;
static const int PIN_NEXT = 44;

static const uint16_t COL_BG = 0xF7BD;
static const uint16_t COL_FG = 0x2610;
static const uint16_t COL_TRY = 0x6F9B;
static const uint16_t COL_ASK = 0xFE70;
static const uint16_t COL_FOOT = 0xE73C;
static const uint16_t COL_SWITCH_ON = 0x07E0;
static const uint16_t COL_SLIDER_TRACK = 0x4208;

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
public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 20000000;
      cfg.freq_read = 8000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
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
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;
static Preferences teacherPrefs;
static volatile bool drawingEnabled = true;
static uint8_t teacherBrightnessPct = 100;
static bool settingsScreen = false;
static uint8_t selectedSetting = 0;
static bool settingsSaveFailed = false;
static BLEServer *teacherServer = nullptr;
static bool studentSettingsPending = true;
static uint8_t connectedPanels = 0;
static unsigned long lastSettingsBroadcastMs = 0;
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
static bool yesWasDown = false;
static bool noWasDown = false;
static bool nextWasDown = false;
static unsigned long nextPressedAt = 0;
static bool nextLongPressHandled = false;
static BLECharacteristic *replyChar = nullptr;
static uint8_t *disk = nullptr;

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

static void applyTeacherBrightness() {
  uint8_t duty = (uint16_t)teacherBrightnessPct * 255 / 100;
  analogWrite(PIN_LCD_BL, duty);
}

static void loadTeacherSettings() {
  if (!teacherPrefs.begin("teacher", true)) {
    Serial.println("Could not read teacher settings; using defaults");
    return;
  }
  drawingEnabled = teacherPrefs.getBool("drawEnabled", true);
  teacherBrightnessPct = teacherPrefs.getUChar("brightness", 100);
  teacherPrefs.end();
  if (teacherBrightnessPct < 20) teacherBrightnessPct = 20;
  if (teacherBrightnessPct > 100) teacherBrightnessPct = 100;
  teacherBrightnessPct = (teacherBrightnessPct / 10) * 10;
}

static bool saveTeacherSettings() {
  if (!teacherPrefs.begin("teacher", false)) {
    Serial.println("Could not open teacher settings for writing");
    return false;
  }
  size_t drawingWritten =
    teacherPrefs.putBool("drawEnabled", drawingEnabled);
  size_t brightnessWritten =
    teacherPrefs.putUChar("brightness", teacherBrightnessPct);
  teacherPrefs.end();
  if (drawingWritten == 0 || brightnessWritten == 0) {
    Serial.println("Could not save all teacher settings");
    return false;
  }
  return true;
}

static void paintTeacherSettings() {
  lcd.fillScreen(COL_BG);
  lcd.fillRect(0, 0, 240, 40, COL_FOOT);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_FOOT);
  lcd.drawString("Settings", 12, 20);
  lcd.setFont(&fonts::Font2);

  lcd.drawRoundRect(8, 54, 224, 62, 8,
                    selectedSetting == 0 ? COL_FG : COL_FOOT);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Student drawing", 16, 73);
  lcd.drawString(drawingEnabled ? "On" : "Off", 16, 96);
  const int switchX = 162;
  const int switchY = 68;
  const int switchW = 62;
  const int switchH = 32;
  uint16_t switchColor = drawingEnabled ? COL_SWITCH_ON : 0x0000;
  lcd.fillRoundRect(switchX, switchY, switchW, switchH, switchH / 2,
                    switchColor);
  lcd.drawRoundRect(switchX, switchY, switchW, switchH, switchH / 2,
                    drawingEnabled ? COL_SWITCH_ON : COL_FG);
  int knobX = drawingEnabled ? switchX + switchW - 16 : switchX + 16;
  lcd.fillCircle(knobX, switchY + switchH / 2, 12, 0xFFFF);

  lcd.drawRoundRect(8, 128, 224, 104, 8,
                    selectedSetting == 1 ? COL_FG : COL_FOOT);
  lcd.drawString("Screen brightness", 16, 146);
  char percent[8];
  snprintf(percent, sizeof(percent), "%u%%",
           (unsigned)teacherBrightnessPct);
  lcd.setTextDatum(textdatum_t::middle_right);
  lcd.drawString(percent, 222, 146);
  lcd.setTextDatum(textdatum_t::top_left);

  const int trackX = 18;
  const int trackY = 182;
  const int trackW = 204;
  const int trackH = 12;
  int fillW = (teacherBrightnessPct - 20) * trackW / 80;
  lcd.fillRoundRect(trackX, trackY, trackW, trackH, 6, COL_SLIDER_TRACK);
  if (fillW > 0) {
    lcd.fillRoundRect(trackX, trackY, fillW, trackH, 6, COL_SWITCH_ON);
  }
  int knobX = trackX + fillW;
  lcd.fillCircle(knobX, trackY + trackH / 2, 9, 0xFFFF);
  lcd.drawCircle(knobX, trackY + trackH / 2, 9, COL_FG);
  lcd.drawString("No: -10", 16, 208);
  lcd.setTextDatum(textdatum_t::top_right);
  lcd.drawString("Yes: +10", 222, 208);

  lcd.fillRect(0, 276, 240, 44, COL_FOOT);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(COL_FG, COL_FOOT);
  lcd.drawString("Yes: On / +10", 8, 288);
  lcd.drawString("No: Off", 88, 288);
  lcd.drawString("Next: Select", 154, 288);
  lcd.drawString(settingsSaveFailed ? "Settings not saved" :
                                      "Hold Next to close", 54, 308);
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintScreen() {
  lcd.fillScreen(COL_BG);
  if (settingsScreen) {
    paintTeacherSettings();
    return;
  }
  lcd.fillRect(0, 288, 240, 32, COL_FOOT);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_FOOT);
  lcd.drawString("Yes", 12, 304);
  lcd.drawString("No", 96, 304);
  lcd.drawString("Next", 176, 304);

  if (waitingCount < 1) {
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(COL_FG, COL_BG);
    lcd.drawString("Waiting for a student", 12, 24);
    lcd.drawString("Hold Next for settings", 12, 270);
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
  lcd.drawString(countBuf, 228, 18);

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
  drawWrapped(note->choice, 12, 190, 216, 264);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("Hold Next for settings", 12, 270);
}

static bool writeFreshLog() {
  File file = SPIFFS.open(LOG_PATH, FILE_WRITE);
  if (!file) {
    return false;
  }
  file.print(LOG_HEADER);
  file.close();
  return true;
}

// A student message carries the time of the tap. This board has no
// battery clock of its own, so it catches up to that time and then
// keeps counting until power is lost.
static void adoptStudentClock(uint32_t timeSent) {
  if (timeSent < 1700000000UL) {
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
// writes a second line with the same id and the time of the button.
static void appendRow(const ClearNote *note, uint32_t timeAnswered) {
  if (!storageOk) {
    return;
  }
  File file = SPIFFS.open(LOG_PATH, FILE_APPEND);
  if (!file) {
    return;
  }
  const char *answer = "";
  if (note->answer == CLEAR_ANSWER_YES) {
    answer = "yes";
  } else if (note->answer == CLEAR_ANSWER_NO) {
    answer = "no";
  }
  file.print(note->eventId);
  file.print(',');
  if (note->timeSent > 0) {
    file.print(note->timeSent);
  }
  file.print(',');
  file.print('"');
  file.print(note->student);
  file.print("\",\"");
  file.print(note->feeling);
  file.print("\",\"");
  file.print(note->choice);
  file.print("\",");
  file.print(note->asksTeacher ? "ask" : "try");
  file.print(',');
  file.print(answer);
  file.print(',');
  if (timeAnswered > 0) {
    file.print(timeAnswered);
  }
  file.println();
  file.close();
}

// Older week files have no time_answered column. Add an empty one so
// later Yes and No lines can store a time without a mismatched header.
static void ensureLogHeader() {
  File file = SPIFFS.open(LOG_PATH, FILE_READ);
  if (!file) {
    return;
  }
  String header = file.readStringUntil('\n');
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
    if (!drawingEnabled) {
      return;
    }
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
  teacherServer = BLEDevice::createServer();
  BLEService *service = teacherServer->createService(CLEAR_SERVICE_UUID);
  BLECharacteristic *notice = service->createCharacteristic(
    CLEAR_NOTICE_UUID, BLECharacteristic::PROPERTY_WRITE);
  notice->setCallbacks(&noticeCallbacks);
  replyChar = service->createCharacteristic(
    CLEAR_REPLY_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  replyChar->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(CLEAR_SERVICE_UUID);
  advertising->start();
}

static void serviceStudentSettings() {
  uint8_t connected = teacherServer == nullptr
                        ? 0
                        : (uint8_t)teacherServer->getConnectedCount();
  if (connected > 0 && connectedPanels == 0) {
    studentSettingsPending = true;
  }
  connectedPanels = connected;
  if (connected == 0 || replyChar == nullptr) {
    return;
  }
  if (!studentSettingsPending &&
      millis() - lastSettingsBroadcastMs < 5000) {
    return;
  }
  ClearStudentSettings packet = {};
  packet.version = CLEAR_LINK_VERSION;
  packet.kind = CLEAR_KIND_STUDENT_SETTINGS;
  packet.drawingEnabled = drawingEnabled ? 1 : 0;
  replyChar->setValue((uint8_t *)&packet, sizeof(packet));
  replyChar->notify();
  studentSettingsPending = false;
  lastSettingsBroadcastMs = millis();
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
      ocrJobRead = (ocrJobRead + 1) % OCR_QUEUE_MAX;
      nextOcrAttemptMs = 0;
      return;
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
  if (!drawingEnabled) {
    noInterrupts();
    drawingQueueOverflow = false;
    drawingOverflowId = 0;
    interrupts();
    drawingRingRead = drawingRingWrite;
    if (activeDrawingId != 0) {
      uint32_t rejectedId = activeDrawingId;
      closeActiveDrawing(true);
      sendDrawingResult(rejectedId, false);
    }
    return;
  }
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

static void sendReply(ClearNote note, uint8_t answer) {
  note.kind = CLEAR_KIND_REPLY;
  note.answer = answer;
  if (replyChar != nullptr) {
    replyChar->setValue((uint8_t *)&note, sizeof(note));
    replyChar->notify();
  }
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

static void setDrawingPreference(bool enabled) {
  if (drawingEnabled == enabled) return;
  drawingEnabled = enabled;
  settingsSaveFailed = !saveTeacherSettings();
  studentSettingsPending = true;
  paintScreen();
}

static void adjustSelectedSetting(bool increase) {
  if (selectedSetting == 0) {
    setDrawingPreference(increase);
    return;
  }
  int brightness = teacherBrightnessPct + (increase ? 10 : -10);
  if (brightness < 20) brightness = 20;
  if (brightness > 100) brightness = 100;
  if (brightness == teacherBrightnessPct) return;
  teacherBrightnessPct = brightness;
  applyTeacherBrightness();
  settingsSaveFailed = !saveTeacherSettings();
  paintScreen();
}

static void handleNextButton() {
  bool down = digitalRead(PIN_NEXT) == LOW;
  if (down && !nextWasDown) {
    nextWasDown = true;
    nextPressedAt = millis();
    nextLongPressHandled = false;
  }
  if (down && nextWasDown && !nextLongPressHandled &&
      millis() - nextPressedAt >= 900) {
    nextLongPressHandled = true;
    settingsScreen = !settingsScreen;
    settingsSaveFailed = false;
    paintScreen();
  }
  if (!down && nextWasDown) {
    nextWasDown = false;
    if (nextLongPressHandled || millis() - nextPressedAt < BUTTON_GAP_MS) {
      return;
    }
    buttonMs = millis();
    if (settingsScreen) {
      selectedSetting = (selectedSetting + 1) % 2;
      paintScreen();
    } else {
      cycleStudent();
    }
  }
}

static bool pressed(int pin, bool *wasDown) {
  bool down = digitalRead(pin) == LOW;
  bool hit = down && !*wasDown && (millis() - buttonMs) >= BUTTON_GAP_MS;
  if (hit) {
    buttonMs = millis();
  }
  *wasDown = down;
  return hit;
}

static void drainNotices() {
  while (ringRead != ringWrite) {
    ClearNote note = noticeRing[ringRead];
    ringRead = (ringRead + 1) % RING_MAX;
    note.answer = CLEAR_ANSWER_NONE;
    adoptStudentClock(note.timeSent);
    appendRow(&note, 0);
    remember(&note);
    paintScreen();
  }
  drainDrawingPackets();
}

void setup() {
  Serial.begin(115200);
  loadTeacherSettings();
  setenv("TZ", CLOCK_TZ, 1);
  tzset();
  pinMode(PIN_LCD_BL, OUTPUT);
  applyTeacherBrightness();
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_YES, INPUT_PULLUP);
  pinMode(PIN_NO, INPUT_PULLUP);
  pinMode(PIN_NEXT, INPUT_PULLUP);
  lcd.init();
  applyTeacherBrightness();
  lcd.setRotation(0);
  paintScreen();
  startStorage();
  startRadio();
  startUsbDrive();
  startOcrWifi();
}

void loop() {
  drainNotices();
  serviceOcrJobs();
  serviceStudentSettings();
  handleNextButton();
  if (pressed(PIN_YES, &yesWasDown)) {
    if (settingsScreen) {
      adjustSelectedSetting(true);
    } else {
      answerCurrent(CLEAR_ANSWER_YES);
    }
  } else if (pressed(PIN_NO, &noWasDown)) {
    if (settingsScreen) {
      adjustSelectedSetting(false);
    } else {
      answerCurrent(CLEAR_ANSWER_NO);
    }
  }
}
