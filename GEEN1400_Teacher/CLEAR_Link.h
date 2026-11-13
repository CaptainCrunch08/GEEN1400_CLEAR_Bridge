// Message shared by the student panel and the teacher device.
// This file is copied in both sketch folders. If one copy changes,
// change the other the same way, including the field order. BLE sends
// these packed structures as they are.
//
// kind 1 is a student selection/request. kind 2 is the teacher's answer.
// answer 0 means no answer yet, 1 is yes, 2 is no, 4 is in a minute.
// answer 5 means the teacher device has the request (pending).
// answer 6 means the teacher is looking at it (seen).
// Yes, no, and in a minute mean the request is answered.
// asksTeacher 1 is an ask. asksTeacher 2 is an accommodation request.
// asksTeacher 3 means the student said this strategy helped.
// asksTeacher 4 means the student said not yet.
// Those two are saved in the week file. They are not a request to answer.
// Kind 11 carries the teacher's drawing preference to the panel.

#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint8_t CLEAR_LINK_VERSION = 2;
static const uint8_t CLEAR_KIND_NOTICE = 1;
static const uint8_t CLEAR_KIND_REPLY = 2;
static const uint8_t CLEAR_KIND_DRAWING_START = 3;
static const uint8_t CLEAR_KIND_DRAWING_CHUNK = 4;
static const uint8_t CLEAR_KIND_DRAWING_END = 5;
static const uint8_t CLEAR_KIND_DRAWING_RESULT = 6;
static const uint8_t CLEAR_KIND_OCR_TEXT_START = 7;
static const uint8_t CLEAR_KIND_OCR_TEXT_CHUNK = 8;
static const uint8_t CLEAR_KIND_OCR_TEXT_END = 9;
static const uint8_t CLEAR_KIND_OCR_TEXT_ACK = 10;
static const uint8_t CLEAR_KIND_STUDENT_SETTINGS = 11;
static const uint8_t CLEAR_ANSWER_NONE = 0;
static const uint8_t CLEAR_ANSWER_YES = 1;
static const uint8_t CLEAR_ANSWER_NO = 2;
// The teacher got the note, but this panel is not on the allowed list yet.
static const uint8_t CLEAR_ANSWER_PENDING = 3;
// The teacher will do this accommodation in a minute.
static const uint8_t CLEAR_ANSWER_LATER = 4;
// The teacher device has the request, and has not opened it yet.
static const uint8_t CLEAR_ANSWER_GOT = 5;
// The request is on the teacher screen.
static const uint8_t CLEAR_ANSWER_SEEN = 6;
// asksTeacher value for an accommodation request. 1 is any other ask.
static const uint8_t CLEAR_ASK_AID = 2;
// The student tapped Yes after a strategy. Not a teacher request.
static const uint8_t CLEAR_ASK_HELPED = 3;
// The student tapped Not yet after a strategy. Not a teacher request.
static const uint8_t CLEAR_ASK_NOTYET = 4;
static const char *CLEAR_BLE_NAME = "CLEAR Teacher";
static const char *CLEAR_BLE_SERVICE_UUID = "4c4c0001-1400-4e45-8e00-000000000001";
static const char *CLEAR_BLE_MESSAGE_UUID = "4c4c0002-1400-4e45-8e00-000000000001";
static const char *CLEAR_BLE_REPLY_UUID = "4c4c0003-1400-4e45-8e00-000000000001";
static const char *CLEAR_SERVICE_UUID = CLEAR_BLE_SERVICE_UUID;
static const char *CLEAR_NOTICE_UUID = CLEAR_BLE_MESSAGE_UUID;
static const char *CLEAR_REPLY_UUID = CLEAR_BLE_REPLY_UUID;
static const uint16_t CLEAR_BLE_MTU = 185;
static const size_t CLEAR_CHOICE_SIZE = 128;
static const size_t CLEAR_DRAWING_CHUNK_SIZE = 140;

#pragma pack(push, 1)
struct ClearNote {
  uint8_t version;
  uint8_t kind;
  uint8_t asksTeacher;
  uint8_t answer;
  uint32_t eventId;
  uint32_t timeSent;
  char student[17];
  char feeling[24];
  char choice[CLEAR_CHOICE_SIZE];
};

struct ClearDrawingStart {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
  uint8_t board;
  uint32_t totalBytes;
  char student[17];
};

struct ClearDrawingChunk {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
  uint16_t chunkIndex;
  uint8_t payloadSize;
  uint8_t payload[CLEAR_DRAWING_CHUNK_SIZE];
};

struct ClearDrawingEnd {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
};

struct ClearDrawingResult {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
  uint8_t saved;
};

struct ClearOcrTextStart {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
  uint8_t board;
  uint16_t totalBytes;
};

struct ClearOcrTextChunk {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
  uint16_t chunkIndex;
  uint8_t payloadSize;
  uint8_t payload[CLEAR_DRAWING_CHUNK_SIZE];
};

struct ClearOcrTextEnd {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
  uint16_t chunkCount;
};

struct ClearOcrTextAck {
  uint8_t version;
  uint8_t kind;
  uint32_t drawingId;
  uint16_t chunkIndex;
  uint8_t accepted;
};

struct ClearStudentSettings {
  uint8_t version;
  uint8_t kind;
  uint8_t drawingEnabled;
};
#pragma pack(pop)

static_assert(sizeof(ClearNote) <= CLEAR_BLE_MTU - 3, "BLE message exceeds configured MTU");
static_assert(sizeof(ClearDrawingChunk) <= CLEAR_BLE_MTU - 3, "Drawing chunk exceeds BLE MTU");
