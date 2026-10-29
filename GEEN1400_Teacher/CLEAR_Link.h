// Message shared by the student panel and the teacher device.
// This file is copied in both sketch folders. If one copy changes,
// change the other the same way, including the field order. ESP-NOW
// replies and BLE requests send these bytes as they are.
//
// kind 1 is a student selection/request. kind 2 is the teacher's Yes or No.
// answer 0 means no answer yet, 1 is yes, 2 is no.

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
static const uint8_t CLEAR_ANSWER_NONE = 0;
static const uint8_t CLEAR_ANSWER_YES = 1;
static const uint8_t CLEAR_ANSWER_NO = 2;
static const char *CLEAR_BLE_NAME = "CLEAR Teacher";
static const char *CLEAR_BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char *CLEAR_BLE_MESSAGE_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char *CLEAR_BLE_REPLY_UUID = "4c4c0003-1400-4e45-8e00-000000000001";
static const char *CLEAR_SERVICE_UUID = CLEAR_BLE_SERVICE_UUID;
static const char *CLEAR_NOTICE_UUID = CLEAR_BLE_MESSAGE_UUID;
static const char *CLEAR_REPLY_UUID = CLEAR_BLE_REPLY_UUID;
static const uint16_t CLEAR_BLE_MTU = 185;
static const size_t CLEAR_CHOICE_SIZE = 128;
static const size_t CLEAR_DRAWING_CHUNK_SIZE = 140;

// Both devices send to this address, so they do not have to be paired.
static uint8_t clearBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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
#pragma pack(pop)

static_assert(sizeof(ClearNote) <= CLEAR_BLE_MTU - 3, "BLE message exceeds configured MTU");
static_assert(sizeof(ClearNote) <= 250, "ESP-NOW message exceeds the supported payload size");
static_assert(sizeof(ClearDrawingChunk) <= CLEAR_BLE_MTU - 3, "Drawing chunk exceeds BLE MTU");
