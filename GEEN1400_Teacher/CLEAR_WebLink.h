// CLEAR_WebLink.h
// Lets the CLEAR Bridge website read this teacher device over USB (Web Serial).
// The website never stores the class PIN or teacher code. It sends them here,
// this device checks them, and only then sends the class week.
//
// ── How to use ─────────────────────────────────────────────────────────────
//   #include "CLEAR_WebLink.h"
//
//   bool checkCodes(const char* pin, const char* code) {
//     // Compare with the class PIN and teacher code this device already uses.
//     return strcmp(pin, classPin) == 0 && strcmp(code, teacherCode) == 0;
//   }
//
//   void setup() {
//     Serial.begin(115200);
//     SD.begin(...);              // or LittleFS.begin(), wherever CLEAR.CSV lives
//     ClearWebLink::begin(SD, "/CLEAR.CSV", "/", checkCodes);
//   }
//
//   void loop() {
//     ClearWebLink::poll();       // call often; it never blocks when nothing arrives
//     ...
//   }
//
// Keep other Serial prints short and on their own lines. The website skips lines
// it does not expect, but a print in the middle of a file transfer would break it.
//
// ── Protocol (115200 baud, one command per line) ───────────────────────────
//   HELLO              -> CLEAR TEACHER 1           (also forgets any sign-in)
//   AUTH <pin> <code>  -> OK | NO | WAIT            (5 wrong tries = 60 s wait)
//   CSV                -> FILE <bytes>\n<raw bytes> | NONE | NO AUTH
//   DRAW               -> COUNT 0                   (drawings stay on the student panel)
//   BYE                -> OK                        (forgets the sign-in)
//   anything else      -> ?
// A sign-in also expires after 10 minutes without a CSV request.
#pragma once
#include <Arduino.h>
#include <FS.h>

namespace ClearWebLink {

typedef bool (*CodeCheck)(const char* pin, const char* code);

static fs::FS* gFs = nullptr;
static const char* gCsvPath = "/CLEAR.CSV";
static const char* gDrawDir = "/";
static CodeCheck gCheck = nullptr;

static bool gAuthed = false;
static uint8_t gFails = 0;
static uint32_t gWaitUntil = 0;
static uint32_t gLastUse = 0;
static char gLine[48];
static uint8_t gLen = 0;

static const uint8_t kMaxTries = 5;
static const uint32_t kWaitMs = 60UL * 1000UL;
static const uint32_t kIdleMs = 10UL * 60UL * 1000UL;

inline void begin(fs::FS& fs, const char* csvPath, const char* drawDir, CodeCheck check) {
  gFs = &fs;
  gCsvPath = csvPath;
  gDrawDir = drawDir;
  gCheck = check;
}

inline bool isSvg(const char* name) {
  size_t n = strlen(name);
  return n > 4 && strcasecmp(name + n - 4, ".svg") == 0;
}

inline void sendFile(File& f) {
  Serial.printf("FILE %u\n", (unsigned)f.size());
  uint8_t buf[512];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    Serial.write(buf, n);
  }
  Serial.flush();
}

inline void handle(char* line) {
  if (strcmp(line, "HELLO") == 0) {
    gAuthed = false;
    Serial.println("CLEAR TEACHER 1");
    return;
  }

  if (strncmp(line, "AUTH ", 5) == 0) {
    if ((int32_t)(millis() - gWaitUntil) < 0) { Serial.println("WAIT"); return; }
    char pin[8] = {0};
    char code[8] = {0};
    bool ok = sscanf(line + 5, "%4s %4s", pin, code) == 2 && gCheck && gCheck(pin, code);
    if (ok) {
      gAuthed = true;
      gFails = 0;
      gLastUse = millis();
      Serial.println("OK");
    } else {
      gAuthed = false;
      if (++gFails >= kMaxTries) {
        gFails = 0;
        gWaitUntil = millis() + kWaitMs;
        Serial.println("WAIT");
      } else {
        Serial.println("NO");
      }
    }
    return;
  }

  if (strcmp(line, "BYE") == 0) {
    gAuthed = false;
    Serial.println("OK");
    return;
  }

  bool wantsCsv = strcmp(line, "CSV") == 0;
  bool wantsDraw = strcmp(line, "DRAW") == 0;
  if (wantsCsv || wantsDraw) {
    if (!gAuthed || millis() - gLastUse > kIdleMs || !gFs) {
      gAuthed = false;
      Serial.println("NO AUTH");
      return;
    }
    gLastUse = millis();

    if (wantsCsv) {
      File f = gFs->open(gCsvPath, "r");
      if (!f || f.isDirectory()) { Serial.println("NONE"); return; }
      sendFile(f);
      f.close();
      return;
    }

    // Student drawings are not stored on this device and are not sent.
    Serial.println("COUNT 0");
    return;
  }

  Serial.println("?");
}

inline void poll() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      gLine[gLen] = 0;
      if (gLen) handle(gLine);
      gLen = 0;
    } else if (gLen < sizeof(gLine) - 1) {
      gLine[gLen++] = (char)c;
    } else {
      gLen = 0;  // line too long; drop it
    }
  }
}

}  // namespace ClearWebLink
