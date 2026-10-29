/*
  GEEN1400 teacher device
  ESP32 mini, buttons, a microSD card, and a 2.5 inch screen.

  Open this folder in the Arduino IDE. Pick the ESP32 board that matches
  the mini you bought. This is not the CrowPanel sketch.

  A student panel sends requests over BLE. This device shows each
  request, stores it on the card, and Yes or No answers the newest
  request. That answer goes back over ESP-NOW.

  The week file is clear-log.csv on the microSD card. To copy it, turn
  the device off, take the card out, and use a USB card reader. The
  ESP32 USB port is for power and programming. It is not a USB drive.
  Copying the file does not erase it.

  Clear week is a separate button. Press Clear, then press Yes within
  8 seconds. That deletes the file and starts a new empty one. No cancels.
  If the file is deleted on the computer instead, the next boot creates
  a new file that has only the column names.

  There is no clock chip, so the time column is seconds since this
  device powered on. The website treats one imported file as one week.

  The 2.5 inch screen is not chosen yet. displayBegin, displayShow, and
  displayStatus are the only functions that should talk to it. Until
  the screen is wired, they print to the Serial Monitor at 115200.
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>
#include <string>
#include "CLEAR_Link.h"

// Change these four numbers to the pins the microSD adapter uses.
// 18, 19, 23, and 5 are the usual SPI pins on a full-size ESP32.
static const int PIN_SD_SCK = 18;
static const int PIN_SD_MISO = 19;
static const int PIN_SD_MOSI = 23;
static const int PIN_SD_CS = 5;

// Buttons connect the pin to ground when pressed. INPUT_PULLUP makes
// an unpressed pin read HIGH, so LOW means the button is down.
static const int PIN_YES = 4;
static const int PIN_NO = 16;
static const int PIN_CLEAR = 17;

static const char *LOG_NAME = "/clear-log.csv";
static const char *DRAWING_DIR = "/student-drawings";
static const int ASK_MAX = 8;
static const int RING_MAX = 4;
static const int DRAWING_RING_MAX = 12;
static const unsigned long CLEAR_WAIT_MS = 8000;
static const unsigned long BUTTON_GAP_MS = 200;

static bool linkReady = false;
static bool sdOk = false;
static bool clearArmed = false;
static unsigned long clearMs = 0;
static unsigned long buttonMs = 0;
static bool yesWasDown = false;
static bool noWasDown = false;
static bool clearWasDown = false;

// Newest selection, whether or not it needs an answer.
static ClearNote latestNote;
static bool hasLatest = false;

// Orange requests still waiting for Yes or No. The newest is last.
static ClearNote asks[ASK_MAX];
static int askCount = 0;

// The radio callback only drops packets here. loop() writes the card.
static ClearNote noticeRing[RING_MAX];
static volatile uint8_t ringWrite = 0;
static uint8_t ringRead = 0;
static BLEServer *bleServer = nullptr;
static bool bleReady = false;
static BLECharacteristic *replyCharacteristic = nullptr;
struct DrawingPacketEntry {
  uint8_t bytes[sizeof(ClearDrawingChunk)];
  uint8_t length;
};
static DrawingPacketEntry drawingRing[DRAWING_RING_MAX];
static volatile uint8_t drawingRingWrite = 0;
static uint8_t drawingRingRead = 0;
static volatile bool drawingQueueOverflow = false;
static File activeDrawingFile;
static uint32_t activeDrawingId = 0;
static uint32_t activeDrawingBytes = 0;
static uint32_t activeDrawingReceived = 0;
static uint16_t activeDrawingChunkIndex = 0;
static char activeDrawingTempPath[64] = "";
static char activeDrawingPath[64] = "";

// =============================================================================
// 2.5 INCH SCREEN
// Replace these three functions when the display and its pins are known.
// The rest of this sketch calls them and should not need to change.
// =============================================================================

static void displayBegin() {
  Serial.begin(115200);
  delay(200);
  Serial.println("CLEAR teacher device");
  Serial.println("The 2.5 inch screen is not wired yet. This monitor is the stand-in.");
}

static void displayStatus(const char *line) {
  Serial.println(line);
}

static void displayShow() {
  Serial.println("---");
  if (!hasLatest) {
    Serial.println("Waiting for a student");
  } else {
    Serial.print(latestNote.student);
    Serial.print("  ");
    Serial.println(latestNote.feeling);
    Serial.println(latestNote.choice);
    if (latestNote.asksTeacher) {
      Serial.println("Ask");
    } else {
      Serial.println("Trying this");
    }
  }
  Serial.print("Open asks: ");
  Serial.println(askCount);
  if (askCount > 0) {
    Serial.print("Yes or No answers: ");
    Serial.print(asks[askCount - 1].student);
    Serial.print(" / ");
    Serial.println(asks[askCount - 1].choice);
  }
  if (!sdOk) {
    Serial.println("No microSD card. This week is not being saved.");
  }
}

// =============================================================================
// END OF 2.5 INCH SCREEN
// =============================================================================

static void writeField(File &file, const char *text) {
  file.print('"');
  if (text != nullptr) {
    for (const char *p = text; *p != 0; p++) {
      if (*p == '"') {
        file.print("\"\"");
      } else {
        file.print(*p);
      }
    }
  }
  file.print('"');
}

// One line of the week file. A notice has an empty teacher_response.
// Yes or No adds a second line with the same id and yes or no filled in.
// The website keeps one event per id and uses the filled-in answer.
static void appendRow(const ClearNote *note) {
  if (!sdOk) {
    return;
  }
  File file = SD.open(LOG_NAME, FILE_APPEND);
  if (!file) {
    displayStatus("Could not write the card");
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
  file.print(millis() / 1000);
  file.print(',');
  writeField(file, note->student);
  file.print(',');
  writeField(file, note->feeling);
  file.print(',');
  writeField(file, note->choice);
  file.print(',');
  writeField(file, note->asksTeacher ? "ask" : "try");
  file.print(',');
  writeField(file, answer);
  file.println();
  file.close();
}

static bool writeHeader() {
  File file = SD.open(LOG_NAME, FILE_WRITE);
  if (!file) {
    return false;
  }
  file.println("id,seconds_on,student,feeling,choice,kind,teacher_response");
  file.close();
  return true;
}

static void startCard() {
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdOk = SD.begin(PIN_SD_CS);
  if (!sdOk) {
    displayStatus("No microSD card");
    return;
  }
  // A missing file means a new week. Create the column names only.
  if (!SD.exists(LOG_NAME)) {
    if (!writeHeader()) {
      sdOk = false;
      displayStatus("Could not create the log");
    }
  }
}

static void clearWeek() {
  if (!sdOk) {
    displayStatus("No card. Nothing to erase.");
    displayShow();
    return;
  }
  SD.remove(LOG_NAME);
  if (!writeHeader()) {
    sdOk = false;
    displayStatus("Could not start a new week file");
  } else {
    displayStatus("Week cleared");
  }
  displayShow();
}

static void rememberAsk(const ClearNote *note) {
  if (!note->asksTeacher) {
    return;
  }
  if (askCount >= ASK_MAX) {
    for (int i = 1; i < ASK_MAX; i++) {
      asks[i - 1] = asks[i];
    }
    askCount = ASK_MAX - 1;
  }
  asks[askCount] = *note;
  askCount++;
}

static void takeNotice(const uint8_t *data, int len) {
  if (len >= 2 && data[0] == CLEAR_LINK_VERSION &&
      data[1] >= CLEAR_KIND_DRAWING_START &&
      data[1] <= CLEAR_KIND_DRAWING_END &&
      len <= (int)sizeof(ClearDrawingChunk)) {
    uint8_t next = (drawingRingWrite + 1) % DRAWING_RING_MAX;
    if (next == drawingRingRead) {
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

class StudentMessageCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    String value = characteristic->getValue();
    takeNotice((const uint8_t *)value.c_str(), value.length());
#else
    std::string value = characteristic->getValue();
    takeNotice((const uint8_t *)value.data(), value.size());
#endif
  }
};

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onLinkRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  takeNotice(data, len);
}
#else
static void onLinkRecv(const uint8_t *mac, const uint8_t *data, int len) {
  (void)mac;
  takeNotice(data, len);
}
#endif

static void startRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(100);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    linkReady = false;
    displayStatus("Teacher radio did not start");
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, clearBroadcast, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    linkReady = false;
    displayStatus("Teacher radio did not start");
    return;
  }
  esp_now_register_recv_cb(onLinkRecv);
  linkReady = true;
}

static void startBle() {
  BLEDevice::init("CLEAR Teacher");
  BLEDevice::setMTU(CLEAR_BLE_MTU);
  bleServer = BLEDevice::createServer();
  if (bleServer == nullptr) {
    bleReady = false;
    displayStatus("Bluetooth server did not start");
    return;
  }
  BLEService *service = bleServer->createService(CLEAR_BLE_SERVICE_UUID);
  if (service == nullptr) {
    bleReady = false;
    displayStatus("Bluetooth service did not start");
    return;
  }
  BLECharacteristic *messageCharacteristic = service->createCharacteristic(
    CLEAR_NOTICE_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  if (messageCharacteristic == nullptr) {
    bleReady = false;
    displayStatus("Bluetooth message channel did not start");
    return;
  }
  messageCharacteristic->setCallbacks(new StudentMessageCallbacks());
  replyCharacteristic = service->createCharacteristic(
    CLEAR_REPLY_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  if (replyCharacteristic == nullptr) {
    bleReady = false;
    displayStatus("Bluetooth reply channel did not start");
    return;
  }
  replyCharacteristic->addDescriptor(new BLE2902());
  ClearNote emptyReply = {};
  emptyReply.version = CLEAR_LINK_VERSION;
  emptyReply.kind = CLEAR_KIND_REPLY;
  emptyReply.answer = CLEAR_ANSWER_NONE;
  replyCharacteristic->setValue((uint8_t *)&emptyReply, sizeof(emptyReply));
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(CLEAR_BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  bleReady = true;
}

static void handleNotice(const ClearNote *note) {
  latestNote = *note;
  hasLatest = true;
  rememberAsk(note);
  ClearNote saved = *note;
  saved.answer = CLEAR_ANSWER_NONE;
  appendRow(&saved);
  displayShow();
}

static void sendDrawingResult(uint32_t drawingId, bool saved) {
  if (replyCharacteristic == nullptr) return;
  ClearDrawingResult result = {};
  result.version = CLEAR_LINK_VERSION;
  result.kind = CLEAR_KIND_DRAWING_RESULT;
  result.drawingId = drawingId;
  result.saved = saved ? 1 : 0;
  replyCharacteristic->setValue((uint8_t *)&result, sizeof(result));
  replyCharacteristic->notify();
}

static void closeActiveDrawing(bool removePartial) {
  if (activeDrawingFile) activeDrawingFile.close();
  if (removePartial && activeDrawingTempPath[0]) {
    SD.remove(activeDrawingTempPath);
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
  if (!sdOk || packet->board > 2 || packet->totalBytes == 0 ||
      packet->totalBytes > 300000) {
    sendDrawingResult(packet->drawingId, false);
    displayStatus("Drawing could not be saved to the card");
    return;
  }
  if (!SD.exists(DRAWING_DIR) && !SD.mkdir(DRAWING_DIR)) {
    sendDrawingResult(packet->drawingId, false);
    displayStatus("Could not create drawing folder");
    return;
  }
  char safeStudent[17];
  for (int i = 0; i < 16; i++) {
    char ch = packet->student[i];
    if (ch == 0) {
      safeStudent[i] = 0;
      break;
    }
    safeStudent[i] = ((ch >= 'A' && ch <= 'Z') ||
                      (ch >= 'a' && ch <= 'z') ||
                      (ch >= '0' && ch <= '9')) ? ch : '_';
    safeStudent[i + 1] = 0;
  }
  const char *boardName = packet->board == 0 ? "graph" :
                          packet->board == 1 ? "whiteboard" : "chalkboard";
  snprintf(activeDrawingPath, sizeof(activeDrawingPath),
           DRAWING_DIR "/%s_%s_%08lX.svg", safeStudent, boardName,
           (unsigned long)packet->drawingId);
  snprintf(activeDrawingTempPath, sizeof(activeDrawingTempPath),
           DRAWING_DIR "/.%s_%s_%08lX.part", safeStudent, boardName,
           (unsigned long)packet->drawingId);
  SD.remove(activeDrawingTempPath);
  activeDrawingFile = SD.open(activeDrawingTempPath, FILE_WRITE);
  if (!activeDrawingFile) {
    closeActiveDrawing(false);
    sendDrawingResult(packet->drawingId, false);
    displayStatus("Could not open drawing file");
    return;
  }
  activeDrawingId = packet->drawingId;
  activeDrawingBytes = packet->totalBytes;
  activeDrawingReceived = 0;
  activeDrawingChunkIndex = 0;
  displayStatus("Receiving a student drawing");
}

static void handleDrawingPacket(const uint8_t *data, uint8_t length) {
  if (length < 2 || data[0] != CLEAR_LINK_VERSION) return;
  if (data[1] == CLEAR_KIND_DRAWING_START &&
      length == sizeof(ClearDrawingStart)) {
    ClearDrawingStart packet;
    memcpy(&packet, data, sizeof(packet));
    packet.student[sizeof(packet.student) - 1] = 0;
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
      displayStatus("Drawing transfer was incomplete");
      return;
    }
    size_t written = activeDrawingFile.write(packet.payload, packet.payloadSize);
    if (written != packet.payloadSize) {
      uint32_t failedId = activeDrawingId;
      closeActiveDrawing(true);
      sendDrawingResult(failedId, false);
      displayStatus("Drawing did not fit on the card");
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
    if (activeDrawingFile) {
      activeDrawingFile.flush();
      activeDrawingFile.close();
    }
    if (complete) {
      if (SD.exists(activeDrawingPath)) SD.remove(activeDrawingPath);
      complete = SD.rename(activeDrawingTempPath, activeDrawingPath);
    }
    if (!complete) SD.remove(activeDrawingTempPath);
    closeActiveDrawing(false);
    sendDrawingResult(finishedId, complete);
    displayStatus(complete ? "Drawing saved to the card" :
                             "Drawing could not be saved to the card");
  }
}

static void drainDrawingPackets() {
  if (drawingQueueOverflow) {
    drawingQueueOverflow = false;
    displayStatus("Drawing transfer queue is full");
  }
  while (drawingRingRead != drawingRingWrite) {
    DrawingPacketEntry packet = drawingRing[drawingRingRead];
    drawingRingRead = (drawingRingRead + 1) % DRAWING_RING_MAX;
    handleDrawingPacket(packet.bytes, packet.length);
  }
}

static void drainNotices() {
  while (ringRead != ringWrite) {
    ClearNote note = noticeRing[ringRead];
    ringRead = (ringRead + 1) % RING_MAX;
    handleNotice(&note);
  }
  drainDrawingPackets();
}

// Yes and No answer the newest orange request, then tell that panel.
static void answerNewest(uint8_t answer) {
  if (askCount == 0) {
    displayStatus("No request is waiting");
    return;
  }
  askCount--;
  ClearNote note = asks[askCount];
  note.kind = CLEAR_KIND_REPLY;
  note.answer = answer;
  if (linkReady) {
    esp_now_send(clearBroadcast, (uint8_t *)&note, sizeof(note));
  }
  if (replyCharacteristic != nullptr) {
    replyCharacteristic->setValue((uint8_t *)&note, sizeof(note));
    replyCharacteristic->notify();
  }
  appendRow(&note);
  displayStatus(answer == CLEAR_ANSWER_YES ? "Sent yes" : "Sent no");
  displayShow();
}

// A press counts once, on the way down, and not again for a short gap.
static bool pressed(int pin, bool *wasDown) {
  bool down = digitalRead(pin) == LOW;
  bool hit = down && !*wasDown && (millis() - buttonMs) >= BUTTON_GAP_MS;
  if (hit) {
    buttonMs = millis();
  }
  *wasDown = down;
  return hit;
}

void setup() {
  displayBegin();
  pinMode(PIN_YES, INPUT_PULLUP);
  pinMode(PIN_NO, INPUT_PULLUP);
  pinMode(PIN_CLEAR, INPUT_PULLUP);
  startRadio();
  startBle();
  startCard();
  displayShow();
}

void loop() {
  drainNotices();

  // Clear stays armed for a few seconds so a later Yes cannot erase
  // the week by accident.
  if (clearArmed && (millis() - clearMs) >= CLEAR_WAIT_MS) {
    clearArmed = false;
    displayStatus("Clear canceled");
    displayShow();
  }

  if (pressed(PIN_CLEAR, &clearWasDown)) {
    clearArmed = true;
    clearMs = millis();
    displayStatus("Press Yes to erase the week. Press No to cancel.");
  } else if (pressed(PIN_YES, &yesWasDown)) {
    if (clearArmed) {
      clearArmed = false;
      clearWeek();
    } else {
      answerNewest(CLEAR_ANSWER_YES);
    }
  } else if (pressed(PIN_NO, &noWasDown)) {
    if (clearArmed) {
      clearArmed = false;
      displayStatus("Clear canceled");
      displayShow();
    } else {
      answerNewest(CLEAR_ANSWER_NO);
    }
  }
}
