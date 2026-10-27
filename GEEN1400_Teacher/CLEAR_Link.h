// Radio message shared by the student panel and the teacher device.
// This file is copied in both sketch folders. If one copy changes,
// change the other the same way, including the field order. ESP-NOW
// sends these bytes as they are. A different order would mix the fields up.
//
// kind 1 is a student selection. kind 2 is the teacher's Yes or No.
// answer 0 means no answer yet, 1 is yes, 2 is no.

#pragma once

#include <stdint.h>

static const uint8_t CLEAR_LINK_VERSION = 1;
static const uint8_t CLEAR_KIND_NOTICE = 1;
static const uint8_t CLEAR_KIND_REPLY = 2;
static const uint8_t CLEAR_ANSWER_NONE = 0;
static const uint8_t CLEAR_ANSWER_YES = 1;
static const uint8_t CLEAR_ANSWER_NO = 2;

// Both devices send to this address, so they do not have to be paired.
static uint8_t clearBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#pragma pack(push, 1)
struct ClearNote {
  uint8_t version;
  uint8_t kind;
  uint8_t asksTeacher;
  uint8_t answer;
  uint32_t eventId;
  char student[17];
  char feeling[24];
  char choice[40];
};
#pragma pack(pop)
