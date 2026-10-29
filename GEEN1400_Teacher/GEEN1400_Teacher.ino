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

  Wiring. The Sense microSD socket already uses D8, D9, and D10, so the
  screen stays off those pins. Tie the LCD RST pin to 3V3.

  LCD VCC  3V3
  LCD GND  GND
  LCD DIN  D5   GPIO6
  LCD CLK  D4   GPIO5
  LCD CS   D3   GPIO4
  LCD DC   D2   GPIO3
  LCD RST  3V3
  LCD BL   D0   GPIO1

  Yes button   D1  GPIO2    other side to GND
  No button    D6  GPIO43   other side to GND
  Next button  D7  GPIO44   other side to GND

  A student panel sends the name, the issue, and the request or solution
  over Bluetooth. This screen opens on the oldest one. Next moves to the
  following student. Yes and No answer the student on screen and send
  that answer back.

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
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <LovyanGFX.hpp>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <esp_heap_caps.h>
#include "CLEAR_Link.h"

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
static const int PIN_YES = 2;
static const int PIN_NO = 43;
static const int PIN_NEXT = 44;

static const uint16_t COL_BG = 0xF7BD;
static const uint16_t COL_FG = 0x2610;
static const uint16_t COL_TRY = 0x6F9B;
static const uint16_t COL_ASK = 0xFE70;
static const uint16_t COL_FOOT = 0xE73C;

static const char *LOG_PATH = "/clear.csv";
static const char *LOG_HEADER =
  "id,time_sent,student,feeling,choice,kind,teacher_response,time_answered\n";

static const int LIST_MAX = 12;
static const int RING_MAX = 8;
static const unsigned long BUTTON_GAP_MS = 200;
static const uint32_t DISK_SECTORS = 256;
static const uint32_t DISK_BYTES = DISK_SECTORS * 512;

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
static bool storageOk = false;
static bool usbMounted = false;
static int viewIndex = 0;
static int waitingCount = 0;
static ClearNote waiting[LIST_MAX];
static ClearNote noticeRing[RING_MAX];
static volatile uint8_t ringWrite = 0;
static uint8_t ringRead = 0;
static unsigned long buttonMs = 0;
static bool yesWasDown = false;
static bool noWasDown = false;
static bool nextWasDown = false;
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

static void paintScreen() {
  lcd.fillScreen(COL_BG);
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
  drawWrapped(note->choice, 12, 190, 216, 280);
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

static void refreshDisk() {
  if (disk == nullptr || !storageOk) {
    return;
  }
  memset(disk, 0, DISK_BYTES);
  uint8_t *boot = disk;
  boot[0] = 0xEB;
  boot[1] = 0x3C;
  boot[2] = 0x90;
  memcpy(boot + 3, "MSDOS5.0", 8);
  boot[11] = 0x00;
  boot[12] = 0x02;
  boot[13] = 1;
  boot[14] = 1;
  boot[16] = 1;
  boot[17] = 16;
  boot[19] = DISK_SECTORS & 0xFF;
  boot[20] = (DISK_SECTORS >> 8) & 0xFF;
  boot[21] = 0xF8;
  boot[22] = 1;
  boot[24] = 1;
  boot[26] = 1;
  boot[38] = 0x29;
  boot[39] = 0x34;
  boot[40] = 0x12;
  memcpy(boot + 43, "CLEAR WEEK ", 11);
  memcpy(boot + 54, "FAT12   ", 8);
  boot[510] = 0x55;
  boot[511] = 0xAA;

  fat12Set(disk + 512, 0, 0xFF8);
  fat12Set(disk + 512, 1, 0xFFF);

  File file = SPIFFS.open(LOG_PATH, FILE_READ);
  uint32_t size = file ? file.size() : 0;
  uint32_t maxData = (DISK_SECTORS - 3) * 512;
  if (size > maxData) {
    size = maxData;
  }
  uint32_t clusters = (size + 511) / 512;
  if (clusters < 1) {
    clusters = 1;
  }
  for (uint32_t c = 0; c < clusters; c++) {
    uint16_t next = (c + 1 == clusters) ? 0xFFF : (uint16_t)(c + 3);
    fat12Set(disk + 512, (int)(c + 2), next);
  }

  uint8_t *dir = disk + 2 * 512;
  memcpy(dir, "CLEAR   ", 8);
  memcpy(dir + 8, "CSV", 3);
  dir[11] = 0x20;
  dir[26] = 2;
  if (file) {
    file.read(disk + 3 * 512, size);
    file.close();
  }
  dir[28] = size & 0xFF;
  dir[29] = (size >> 8) & 0xFF;
  dir[30] = (size >> 16) & 0xFF;
  dir[31] = (size >> 24) & 0xFF;
}

static bool diskHasLog() {
  if (disk == nullptr) {
    return false;
  }
  uint8_t *dir = disk + 2 * 512;
  for (int i = 0; i < 16; i++) {
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
    if (characteristic->getValue().length() != sizeof(ClearNote)) {
      return;
    }
    takeNotice(characteristic->getData(), sizeof(ClearNote));
  }
};

static NoticeCallbacks noticeCallbacks;

static void startRadio() {
  BLEDevice::init(CLEAR_BLE_NAME);
  BLEDevice::setMTU(CLEAR_BLE_MTU);
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(CLEAR_SERVICE_UUID);
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
}

void setup() {
  setenv("TZ", CLOCK_TZ, 1);
  tzset();
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_YES, INPUT_PULLUP);
  pinMode(PIN_NO, INPUT_PULLUP);
  pinMode(PIN_NEXT, INPUT_PULLUP);
  lcd.init();
  lcd.setRotation(0);
  paintScreen();
  startStorage();
  startRadio();
  startUsbDrive();
}

void loop() {
  drainNotices();
  if (pressed(PIN_NEXT, &nextWasDown)) {
    cycleStudent();
  } else if (pressed(PIN_YES, &yesWasDown)) {
    answerCurrent(CLEAR_ANSWER_YES);
  } else if (pressed(PIN_NO, &noWasDown)) {
    answerCurrent(CLEAR_ANSWER_NO);
  }
}
