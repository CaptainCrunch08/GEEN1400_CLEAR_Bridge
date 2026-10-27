/*
  GEEN1400 teacher device
  ESP32 mini, buttons, a microSD card, and a 2.5 inch screen.

  Open this folder in the Arduino IDE. Pick the ESP32 board that matches
  the mini you bought. This is not the CrowPanel sketch.

  A student panel sends every selection over the radio. This device
  shows it, stores it on the card, and Yes or No answers the newest
  orange request. That answer goes back to the student panel.

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
#include <SD.h>
#include <SPI.h>
#include <string.h>
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
static const int ASK_MAX = 8;
static const int RING_MAX = 4;
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

static void handleNotice(const ClearNote *note) {
  latestNote = *note;
  hasLatest = true;
  rememberAsk(note);
  ClearNote saved = *note;
  saved.answer = CLEAR_ANSWER_NONE;
  appendRow(&saved);
  displayShow();
}

static void drainNotices() {
  while (ringRead != ringWrite) {
    ClearNote note = noticeRing[ringRead];
    ringRead = (ringRead + 1) % RING_MAX;
    handleNotice(&note);
  }
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
