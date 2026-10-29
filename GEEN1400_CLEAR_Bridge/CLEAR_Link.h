// Message shared by the student panel and the teacher device.
// Keep both copies byte-for-byte identical, including the field order.
// Student requests include their send time and up to 127 bytes of choice text.

#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint8_t CLEAR_LINK_VERSION = 2;
static const uint8_t CLEAR_KIND_NOTICE = 1;
static const uint8_t CLEAR_KIND_REPLY = 2;
static const uint8_t CLEAR_ANSWER_NONE = 0;
static const uint8_t CLEAR_ANSWER_YES = 1;
static const uint8_t CLEAR_ANSWER_NO = 2;

static const char *CLEAR_BLE_NAME = "CLEAR Teacher";
static const char *CLEAR_SERVICE_UUID = "4c4c0001-1400-4e45-8e00-000000000001";
static const char *CLEAR_NOTICE_UUID = "4c4c0002-1400-4e45-8e00-000000000001";
static const char *CLEAR_REPLY_UUID = "4c4c0003-1400-4e45-8e00-000000000001";
static const uint16_t CLEAR_BLE_MTU = 185;
static const size_t CLEAR_CHOICE_SIZE = 128;

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
#pragma pack(pop)

static_assert(sizeof(ClearNote) <= CLEAR_BLE_MTU - 3, "BLE message exceeds configured MTU");
