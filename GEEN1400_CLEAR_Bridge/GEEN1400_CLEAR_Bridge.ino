/*
  GEEN1400 — Scott Nickerson
  Elecrow CrowPanel 5.0 (800x480), ESP32-S3 on the display board.

  Open this folder in the Arduino IDE, not the Uno sketch next door.
  Board: ESP32S3 Dev Module
  PSRAM: OPI PSRAM
  Flash Size: 4MB
  Partition Scheme: Huge APP (3MB No OTA)
  Library: LovyanGFX (Library Manager)

  USB-C on the CrowPanel is the program port. If upload sits on
  "Connecting...", hold BOOT, tap RST, and release BOOT.

  A student selection is sent by Bluetooth to the teacher device. There
  is no phone app. The teacher answers Yes or No, and that answer shows
  along the bottom of this panel. The time sent with each selection
  comes from this panel's clock.

  Green buttons are something the student can try.
  Orange buttons ask the teacher. Both are sent, so the teacher knows
  what the student chose. Opening a list does not send. A typed message
  sends when the student presses Send. Box breathing sends when it is
  opened, and the count starts when the student presses Begin.

  The gear in the top right opens settings. A teacher can change the
  name on the startup screen, the brightness, and the type size.
  Kindergarten and grades 1-2 show a small picture beside the words.
  Younger grades use bigger letters. Grade 5 uses smaller type.
  The three boxes on the first page fill the space under the title.

  How this sketch runs:
  setup() runs once at power-on. loop() then repeats forever.
  There is no web page on the panel. Each paint function draws one
  whole screen. A finger tap is noticed in loop(), and that changes
  which screen variable is current. The next paint draws that screen.
  The name, brightness, and grade are written to flash, so they are
  still there after the panel restarts.
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <SPIFFS.h>
#include <Wire.h>          // I2C bus, used by the touch chip and the small expander
#include <Preferences.h>   // saves a few settings in flash across restarts
#include <esp_system.h>
#include <LovyanGFX.hpp>   // draws text, rectangles, and reads the touch panel
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "CLEAR_Link.h"    // radio message shared with the teacher sketch

// LGFX is the display driver. The constructor only stores the wiring.
// Nothing is drawn until lcd.init() in setup().
// The numbers are the ESP32 pins soldered to this CrowPanel. They match
// Elecrow's 5 inch board and should not be changed for a different project
// unless the board itself is different.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Touch_GT911 _touch_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void) {
    // Sixteen color wires plus the sync signals that tell the panel
    // when each new row and frame starts. freq_write is the pixel clock.
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0 = GPIO_NUM_8;
      cfg.pin_d1 = GPIO_NUM_3;
      cfg.pin_d2 = GPIO_NUM_46;
      cfg.pin_d3 = GPIO_NUM_9;
      cfg.pin_d4 = GPIO_NUM_1;
      cfg.pin_d5 = GPIO_NUM_5;
      cfg.pin_d6 = GPIO_NUM_6;
      cfg.pin_d7 = GPIO_NUM_7;
      cfg.pin_d8 = GPIO_NUM_15;
      cfg.pin_d9 = GPIO_NUM_16;
      cfg.pin_d10 = GPIO_NUM_4;
      cfg.pin_d11 = GPIO_NUM_45;
      cfg.pin_d12 = GPIO_NUM_48;
      cfg.pin_d13 = GPIO_NUM_47;
      cfg.pin_d14 = GPIO_NUM_21;
      cfg.pin_d15 = GPIO_NUM_14;
      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync = GPIO_NUM_41;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 15000000;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 43;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 12;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      _bus_instance.config(cfg);
    }
    // The glass is 800 pixels wide and 480 tall. Offset stays 0 because
    // the image starts at the corner of the panel, not inset.
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width = 800;
      cfg.memory_height = 480;
      cfg.panel_width = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    // GT911 touch chip, on the same I2C pins as the expander (19 and 20).
    // x and y here are pixel coordinates. A tap at the left edge is x = 0.
    // Address 0x5D is the address this panel answers on.
    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 799;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = -1;
      cfg.pin_rst = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x5D;
      cfg.pin_sda = 19;
      cfg.pin_scl = 20;
      // 100 kHz. At 400 kHz this panel drops touch reports.
      cfg.freq = 100000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    // Backlight on pin 2. PWM turns the light partly on instead of only
    // fully on or off, which is how the brightness slider works.
    // 20000 Hz is fast enough that the light does not visibly flicker.
    // invert false means a higher number is brighter.
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 2;
      cfg.freq = 20000;
      cfg.pwm_channel = 7;
      cfg.invert = false;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};

// Pin 2 is also driven as a simple on/off output for a moment in setup(),
// before the PWM backlight takes over the same pin.
static const uint8_t PIN_BL = 2;
// Small I2C chip on the panel. One of its pins helps enable the backlight.
static const uint8_t PCA9557_ADDR = 0x19;
// The greeting stays up this long, then the feelings list appears on its own.
static const unsigned long HELLO_MS = 2000;
// Ignore a second tap that arrives sooner than this. A finger often
// sends more than one touch for a single press.
static const unsigned long TAP_GAP_MS = 200;

// currentScreen is always one of these. Drawing and tap handling both
// look at it, so the program knows which page the student is on.
static const int SCREEN_HELLO = 0;      // "Hello, name"
static const int SCREEN_MENU = 1;       // a list of choices
static const int SCREEN_SUBMENU = 2;    // kept so older checks still compile
static const int SCREEN_SELECTED = 3;   // steps, or the breathing count
static const int SCREEN_SETTINGS = 4;   // name, brightness, grade
static const int SCREEN_NAME = 5;       // keyboard for a name or a message
static const int SCREEN_WRITE = 6;      // three lines for "how I feel"
static const int SCREEN_DRAW_MENU = 7;  // choose a drawing surface
static const int SCREEN_DRAW_CANVAS = 8; // draw on the selected surface

static const int DRAW_HEADER_H = 108;
static const int DRAW_CANVAS_BOTTOM = 420;
static const int DRAW_BACK_X = 8;
static const int DRAW_BACK_Y = 8;
static const int DRAW_BACK_W = 82;
static const int DRAW_BACK_H = 44;
static const int DRAW_SAVE_X = 98;
static const int DRAW_RESET_X = 192;
static const int DRAW_NEW_X = 310;
static const int DRAW_BUTTON_Y = 8;
static const int DRAW_BUTTON_H = 44;
static const int DRAW_COLOR_Y = 58;
static const int DRAW_COLOR_W = 64;
static const int DRAW_COLOR_H = 36;
static const int DRAW_COLOR_X[] = {504, 572, 640, 708};
static const int DRAW_COLOR_COUNT = 4;
static const uint16_t DRAW_CHALK_COLORS[DRAW_COLOR_COUNT] = {
  0xFD20, 0xFFFF, 0x001F, 0x07E0
};
static const char *DRAW_CHALK_NAMES[DRAW_COLOR_COUNT] = {
  "Orange", "White", "Blue", "Green"
};
static const uint32_t DRAW_FILE_MAGIC = 0x434C4452;
static const uint8_t DRAW_FILE_VERSION = 1;
static const int DRAW_SEGMENT_MAX = 2400;
static const unsigned long DRAW_WARNING_MS = 120000;

static const int BOARD_GRAPH = 0;
static const int BOARD_WHITE = 1;
static const int BOARD_CHALK = 2;

#pragma pack(push, 1)
struct DrawingFileHeader {
  uint32_t magic;
  uint32_t drawingId;
  uint32_t savedAt;
  uint16_t segmentCount;
  uint8_t version;
  uint8_t board;
  uint8_t chalkColor;
};

struct DrawingSegment {
  uint16_t x1;
  uint16_t y1;
  uint16_t x2;
  uint16_t y2;
  uint16_t color;
  uint8_t width;
};
#pragma pack(pop)

// Positions are pixels. x grows to the right, y grows downward.
// (0, 0) is the top-left corner. The screen is 800 by 480.
// Feeling and response buttons share one column. The list starts
// below the title and stops above the status strip at the bottom.
static const int ROW_X = 24;
static const int ROW_W = 752;
static const int ROW_GAP = 6;
static const int LIST_TOP = 64;
static const int LIST_BOTTOM = 412;
// Gear sits in the top-right corner. Back sits just to its left on
// the screens that need a way to return. Done and Save reuse the
// top-right corner on the settings screens, where the gear is hidden.
static const int GEAR_W = 56;
static const int GEAR_H = 44;
static const int GEAR_X = 800 - 12 - GEAR_W;
static const int GEAR_Y = 8;
static const int BACK_W = 140;
static const int BACK_H = 44;
static const int BACK_X = GEAR_X - 8 - BACK_W;
static const int BACK_Y = 8;
static const int DONE_W = 140;
static const int DONE_H = 44;
static const int DONE_X = 800 - 12 - DONE_W;
static const int DONE_Y = 8;
static const int NAME_BACK_W = 140;
static const int NAME_BACK_H = 44;
static const int NAME_BACK_X = DONE_X - 8 - NAME_BACK_W;
static const int NAME_BACK_Y = 8;

// Layout of the settings page and the name keyboard, in pixels.
// STUDENT_NAME_MAX is the longest name that fits in the saved setting and
// on the greeting line.
static const int STUDENT_NAME_MAX = 16;
// A typed message plus its lead must fit in the teacher note.
static const int MESSAGE_MAX = 39;
static const int WRITE_MAX = 28;
static const int TYPE_NAME = 0;
static const int TYPE_MESSAGE = 1;
static const int TYPE_WRITE = 2;
static const int NAME_Y = 60;
static const int NAME_H = 56;
static const int CHANGE_W = 128;
static const int CHANGE_H = 40;
static const int CHANGE_X = ROW_X + ROW_W - 12 - CHANGE_W;
static const int CHANGE_Y = NAME_Y + 8;

static const int BRIGHT_LABEL_Y = 126;
static const int BRIGHT_Y = 148;
static const int BRIGHT_H = 48;
static const int STEP_W = 56;
static const int MINUS_X = 24;
static const int PLUS_X = ROW_X + ROW_W - STEP_W;
static const int PCT_W = 72;
static const int PCT_X = PLUS_X - 8 - PCT_W;
static const int BAR_X = MINUS_X + STEP_W + 8;
static const int BAR_W = PCT_X - 8 - BAR_X;
// Brightness never goes below 20, so a tap cannot turn the panel black.
static const int BRIGHT_MIN = 20;
static const int BRIGHT_MAX = 100;

static const int GRADE_NOTE_Y = 206;
static const int GRADE_TOP = 230;
static const int GRADE_H = 56;
static const int GRADE_GAP = 6;
static const int GRADE_COUNT = 4;
// These match the four rows in gradeOptions, top to bottom.
static const int GRADE_K = 0;
static const int GRADE_12 = 1;
static const int GRADE_34 = 2;
static const int GRADE_5 = 3;
// First boot uses grades 3-4 until a teacher picks another row.
static const int GRADE_LIVE = GRADE_34;

static const int FIELD_X = 24;
static const int FIELD_Y = 68;
static const int FIELD_W = 752;
static const int FIELD_H = 64;
static const int KEY_LEFT = 16;
static const int KEY_AREA_W = 768;
static const int KEY_TOP = 148;
static const int KEY_H = 62;
static const int KEY_GAP = 8;
static const int KEY_MAX = 32;

// What a keyboard key does when it is tapped. Letter keys use KEY_CHAR.
static const int KEY_CHAR = 1;
static const int KEY_BACKSPACE = 2;
static const int KEY_CLEAR = 3;
static const int KEY_SPACE = 4;

// =============================================================================
// =============================================================================
//
//   EDIT THE STUDENT UI TEXT AND COLORS HERE
//
//   This block is the words and colors on the panel:
//   screen colors, grade-level rows, and every choice the student can tap.
//   Change the text in quotes to change what a student reads.
//   ACT_ASK and ACT_TYPE are orange. The teacher is asked to answer.
//   ACT_TRY, ACT_BREATHE, and ACT_WRITE are green. The teacher is told,
//   and is not asked to decide. ACT_OPEN only opens the next list.
//   little is the one word K shows on the button. The teacher still
//   receives the full label. A typed message is cut to 39 characters,
//   which is all the teacher note can hold.
//
//   How those words are drawn is the next big block, at paintHello().
//
// =============================================================================
// =============================================================================

// Colors are 16-bit values, the format this display uses (5 bits red,
// 6 bits green, 5 bits blue), not ordinary web hex. The names are what
// they look like on the panel: page background, text, button gray,
// green for "try this", orange for "ask the teacher", and the footer.
static const uint16_t COL_BG = 0xF7BD;
static const uint16_t COL_FG = 0x2610;
static const uint16_t COL_ROW = 0x6F5F;
static const uint16_t COL_TRY = 0x6F9B;
static const uint16_t COL_ASK = 0xFE70;
static const uint16_t COL_FOOT = 0xE73C;

// One age group in settings. about is the one-line description of its look.
// Picking a row changes the feelings screens, not only this list.
struct GradeOption {
  const char *label;
  const char *about;
};

static const GradeOption gradeOptions[] = {
  {"K", "Biggest letters"},
  {"1 - 2", "Big letters"},
  {"3 - 4", "More words"},
  {"5", "Smaller type"}
};

// What a button does. ACT_OPEN does not tell the teacher.
// The others tell the teacher once, when the choice is real.
static const uint8_t ACT_OPEN = 0;
static const uint8_t ACT_TRY = 1;
static const uint8_t ACT_ASK = 2;
static const uint8_t ACT_TYPE = 3;
static const uint8_t ACT_BREATHE = 4;
static const uint8_t ACT_WRITE = 5;
static const uint8_t ACT_DRAWING = 6;

// One button. label is the full words the teacher receives.
// little is the short word K sees. lead is added in front of
// typed text. kids is the next list for ACT_OPEN.
struct Item {
  const char *label;
  const char *topic;
  const char *help;
  const char *little;
  uint8_t action;
  const char *lead;
  const Item *kids;
  uint8_t kidCount;
};

static const char *ASK_HELP = "Your teacher can say yes or no.\nThey see it on their device.";
static const char *TYPE_HELP = "Type a short message, then press Send.";
static const char *BREATH_HELP = "Breathe in for 4 seconds.\nHold for 4 seconds.\nBreathe out for 4 seconds.\nHold for 4 seconds.\nPress Begin and count along.";

static const Item distractedKids[] = {
  {"Box breathing", "Distracted", BREATH_HELP, "Breathe", ACT_BREATHE, nullptr, nullptr, 0},
  {"Quieter class please", "Distracted", ASK_HELP, "Quiet", ACT_ASK, nullptr, nullptr, 0},
  {"Ask for a fidget toy", "Distracted", ASK_HELP, "Fidget", ACT_ASK, nullptr, nullptr, 0},
  {"Custom message", "Distracted", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item worriedKids[] = {
  {"Talk to the teacher", "Worried", ASK_HELP, "Talk", ACT_ASK, nullptr, nullptr, 0},
  {"Box breathing", "Worried", BREATH_HELP, "Breathe", ACT_BREATHE, nullptr, nullptr, 0},
  {"Positive self-talk", "Worried", "1. Notice the words in your head.\n2. Ask: is that fair?\n3. Say one kinder true sentence.\n\nI can try one small step.\nI can ask for help.", "Kind", ACT_TRY, nullptr, nullptr, 0},
  {"Put the worry outside", "Worried", "1. Name the worry.\n2. Say it like it is outside you.\n3. Name one thing you can do.", "Worry", ACT_TRY, nullptr, nullptr, 0},
  {"Custom message", "Worried", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item upsetKids[] = {
  {"Could I take a walk?", "Upset", ASK_HELP, "Walk", ACT_ASK, nullptr, nullptr, 0},
  {"Box breathing", "Upset", BREATH_HELP, "Breathe", ACT_BREATHE, nullptr, nullptr, 0},
  {"Talk in private", "Upset", ASK_HELP, "Private", ACT_ASK, nullptr, nullptr, 0},
  {"Talk to somebody", "Upset", "Ask to talk with somebody,\nsuch as a counselor.", "Someone", ACT_ASK, nullptr, nullptr, 0},
  {"Use the COPE steps", "Upset", "C  Calm your body.\nO  Notice the thought. Change it.\nP  Plan one other thing to do.\nE  Ask: do I feel cooler or hotter?", "COPE", ACT_TRY, nullptr, nullptr, 0},
  {"Custom message", "Upset", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item frustratedKids[] = {
  {"Box breathing", "Frustrated", BREATH_HELP, "Breathe", ACT_BREATHE, nullptr, nullptr, 0},
  {"Could I take a walk?", "Frustrated", ASK_HELP, "Walk", ACT_ASK, nullptr, nullptr, 0},
  {"Custom message", "Frustrated", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item uncomfortableKids[] = {
  {"Write how you feel", "Uncomfortable", "Write a few words on the next screen.\nYour teacher sees that you wrote.", "Write", ACT_WRITE, nullptr, nullptr, 0},
  {"Ask to take a break", "Uncomfortable", ASK_HELP, "Break", ACT_ASK, nullptr, nullptr, 0},
  {"Custom message", "Uncomfortable", TYPE_HELP, "Message", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item personalKids[] = {
  {"Distracted", "Distracted", nullptr, "Busy", ACT_OPEN, nullptr, distractedKids, 4},
  {"Worried", "Worried", nullptr, "Worried", ACT_OPEN, nullptr, worriedKids, 5},
  {"Upset", "Upset", nullptr, "Upset", ACT_OPEN, nullptr, upsetKids, 6},
  {"Frustrated", "Frustrated", nullptr, "Mad", ACT_OPEN, nullptr, frustratedKids, 3},
  {"Uncomfortable", "Uncomfortable", nullptr, "Uneasy", ACT_OPEN, nullptr, uncomfortableKids, 3}
};
static const Item materialKids[] = {
  {"Ask a friend for help", "Help", "Try asking a friend\nbefore you ask the teacher.", "Friend", ACT_TRY, nullptr, nullptr, 0},
  {"Explain the last problem", "Help", ASK_HELP, "Again", ACT_ASK, nullptr, nullptr, 0},
  {"Explain this problem", "Help", ASK_HELP, "Explain", ACT_ASK, nullptr, nullptr, 0},
  {"I need more time", "Help", ASK_HELP, "Time", ACT_ASK, nullptr, nullptr, 0},
  {"Repeat the directions", "Help", ASK_HELP, "Again", ACT_ASK, nullptr, nullptr, 0},
  {"I need teacher help", "Help", ASK_HELP, "Help", ACT_ASK, nullptr, nullptr, 0},
  {"Show another example", "Help", ASK_HELP, "Example", ACT_ASK, nullptr, nullptr, 0}
};
static const Item questionKids[] = {
  {"Repeat a part", "Question", "Type the part you want repeated.", "Part", ACT_TYPE, "Repeat the part about ", nullptr, 0},
  {"Repeat the last question", "Question", ASK_HELP, "Again", ACT_ASK, nullptr, nullptr, 0},
  {"Repeat one question", "Question", "Type which question.", "Which", ACT_TYPE, "Question about ", nullptr, 0},
  {"Ask another student", "Question", "Try this if the teacher is not speaking.", "Friend", ACT_TRY, nullptr, nullptr, 0},
  {"Try raising your hand", "Question", "Put your hand up and wait.", "Hand", ACT_TRY, nullptr, nullptr, 0},
  {"Custom message", "Question", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item helpKids[] = {
  {"Help with the material", "Help", nullptr, "Work", ACT_OPEN, nullptr, materialKids, 7},
  {"I need personal help", "Help", nullptr, "Me", ACT_OPEN, nullptr, personalKids, 5},
  {"Talk in private", "Help", ASK_HELP, "Private", ACT_ASK, nullptr, nullptr, 0}
};
static const Item mainItems[] = {
  {"I have a question", "Question", nullptr, "Ask", ACT_OPEN, nullptr, questionKids, 6},
  {"I need a break", "Break", ASK_HELP, "Break", ACT_ASK, nullptr, nullptr, 0},
  {"I need help", "Help", nullptr, "Help", ACT_OPEN, nullptr, helpKids, 3},
  {"Drawing Board", "Drawing Board", nullptr, "Draw", ACT_DRAWING, nullptr, nullptr, 0}
};
static const int MAIN_COUNT = 4;

// =============================================================================
// =============================================================================
//
//   END OF STUDENT UI TEXT AND COLORS
//
// =============================================================================
// =============================================================================

// One on-screen key: what it does, the letter it types, and the
// rectangle used both to draw it and to test a tap.
struct KeyHit {
  int action;
  char ch;
  int x;
  int y;
  int w;
  int h;
};

// Values that have to survive from one loop() pass to the next.
// If they were created inside loop(), they would reset every time.
static LGFX lcd;
static Preferences prefs;             // handle for the flash settings
static bool expanderOk = false;       // true after the small I2C chip answers
static int currentScreen = SCREEN_HELLO;
static int returnScreen = SCREEN_MENU; // screen to reopen when settings closes
static unsigned long screenMs = 0;    // when the greeting was shown
static unsigned long tapMs = 0;       // when the last accepted tap happened
static bool touchWasDown = false;     // so one press counts once, not every loop
// The touch chip's address. Most of these panels answer at 0x5D.
// A few answer at 0x14, and setup() switches if 0x5D is silent.
static uint8_t touchAddr = 0x5D;
// True while a finger is on the glass. fingerX and fingerY are the
// last place it was reported, in screen pixels.
static bool fingerDown = false;
static int32_t fingerX = 0;
static int32_t fingerY = 0;
static unsigned long fingerMs = 0;
static bool nameDirty = false;        // name changed, so Done should show the greeting
static bool linkReady = false;        // true after Bluetooth starts
static bool noticeSent = false;       // true after a selection reaches the teacher
static bool needSend = false;         // a selection is waiting to be sent
static uint32_t nextEventId = 1;      // counts selections so a reply finds the right one
static uint32_t waitingId = 0;        // event the panel is waiting on
static uint8_t teacherReply = CLEAR_ANSWER_NONE;
static ClearNote pendingReply;
static ClearNote outboundNote;
static volatile bool replyReady = false;
static BLEClient *teacherClient = nullptr;
static BLERemoteCharacteristic *noticeRemote = nullptr;
static BLERemoteCharacteristic *replyRemote = nullptr;
static unsigned long nextBleMs = 0;
// The teacher search runs in the background. scanBusy means it has
// started. scanDone is set when the one-second search finishes, so
// loop() can keep reading the touch chip the whole time.
static bool scanBusy = false;
static volatile bool scanDone = false;
static unsigned long scanStartMs = 0;
static unsigned long clockMs = 0;
static char studentName[STUDENT_NAME_MAX + 1] = "Connor"; // name on the greeting
static char nameEdit[STUDENT_NAME_MAX + 1] = "Connor";    // name being typed, not saved yet
static char messageEdit[MESSAGE_MAX + 1] = "";
static char writeLines[3][WRITE_MAX + 1] = {};
static int writeSlot = 0;
static int typeMode = TYPE_NAME;
static int brightnessPct = 80;
static int gradeLevel = GRADE_LIVE;
static int selectedBoard = BOARD_GRAPH;
static int lastDrawX = -1;
static int lastDrawY = -1;
static bool drawingStroke = false;
static bool drawStorageReady = false;
static DrawingSegment drawingSegments[DRAW_SEGMENT_MAX];
static int drawingSegmentCount = 0;
static uint32_t currentDrawingId = 0;
static uint8_t selectedChalkColor = 1;
static int cursorX = -1;
static int cursorY = -1;
static bool cursorVisible = false;
static bool storageWarningActive = false;
static unsigned long storageWarningStartedMs = 0;
static bool storageSavePending = false;
static bool storageCleanupRunning = false;
static bool drawingSyncPending = false;
static bool drawingSyncWaitingForAck = false;
static bool newBoardAfterSync = false;
static uint8_t drawingSyncState = 0;
static uint16_t drawingSyncChunkIndex = 0;
static uint32_t drawingSyncBytesSent = 0;
static uint32_t drawingSyncId = 0;
static unsigned long drawingSyncStartedMs = 0;
static File drawingSyncFile;
static char drawingSyncPath[40] = "";
static volatile bool drawingResultReady = false;
static ClearDrawingResult pendingDrawingResult = {};
static char drawStatus[64] = "Draw on the board";
static unsigned long drawStatusUntilMs = 0;

// The list on screen, and the lists the student came from.
// crumbTop is how many lists are underneath the one on screen.
struct MenuCrumb {
  const Item *items;
  int count;
  const char *title;
};
static MenuCrumb crumbs[5];
static int crumbTop = 0;
static const Item *viewItems = mainItems;
static int viewCount = MAIN_COUNT;
static const char *viewTitle = "What do you need?";
static const Item *openItem = nullptr;
static bool breathRunning = false;
static unsigned long breathMs = 0;
static unsigned long breathDrawMs = 0;

// Send one byte to a register on the PCA9557 over I2C.
// Returns true when the chip acknowledges. The register numbers
// come from that chip's datasheet: 0x03 is direction, 0x01 is output.
static bool pcaWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(PCA9557_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Start I2C on pins 19 and 20, the same pins the touch chip uses.
// If the expander does not answer, later backlight writes are skipped
// and the PWM pin still controls the light by itself.
// On this chip a 1 in register 3 means "input" and a 0 means "output".
// The sequence drives every pin low, waits, raises pin 0 for the
// backlight enable, then sets pin 1 back to an input.
static void initExpander() {
  Wire.begin(19, 20);
  Wire.beginTransmission(PCA9557_ADDR);
  expanderOk = Wire.endTransmission() == 0;
  if (!expanderOk) {
    return;
  }
  pcaWrite(0x03, 0x00);
  pcaWrite(0x01, 0x00);
  delay(20);
  pcaWrite(0x01, 0x01);
  pcaWrite(0x03, 0x02);
}

// Copy a name into a fixed buffer and always end it with a 0,
// which is how C marks the end of a string. Stops at STUDENT_NAME_MAX
// so a long saved value cannot write past the array.
static void copyName(char *dest, const char *src) {
  int i = 0;
  while (src[i] && i < STUDENT_NAME_MAX) {
    dest[i] = src[i];
    i++;
  }
  dest[i] = 0;
}

// Turn a percent from 20 to 100 into the 0-255 level setBrightness expects.
// 20% becomes 50, not 0, so the panel stays readable at the bottom of the slider.
// 100% becomes 255, which is full brightness.
static int dutyForBrightness(int pct) {
  if (pct < BRIGHT_MIN) {
    pct = BRIGHT_MIN;
  }
  if (pct > BRIGHT_MAX) {
    pct = BRIGHT_MAX;
  }
  return 50 + (205 * (pct - BRIGHT_MIN)) / (BRIGHT_MAX - BRIGHT_MIN);
}

// Push the saved percent out to the backlight, and make sure the
// expander's enable pin is still on if that chip was found.
static void applyBrightness() {
  lcd.setBrightness(dutyForBrightness(brightnessPct));
  if (expanderOk) {
    pcaWrite(0x01, 0x01);
  }
}

// Read the saved name, brightness, and grade. The second argument
// to each get is what we use when nothing has been saved yet.
// The true on begin() means read-only. Names are checked character
// by character so a damaged save cannot put odd symbols on the greeting.
static void loadSettings() {
  prefs.begin("clear", true);
  String stored = prefs.getString("name", "Connor");
  brightnessPct = prefs.getInt("bright", 80);
  gradeLevel = prefs.getInt("grade", GRADE_LIVE);
  prefs.end();

  int n = 0;
  for (int i = 0; i < stored.length() && n < STUDENT_NAME_MAX; i++) {
    char c = stored.charAt(i);
    bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    bool mark = (c == ' ' || c == '-' || c == '\'');
    if (!letter && !mark) {
      continue;
    }
    if (c == ' ' && n == 0) {
      continue;
    }
    studentName[n++] = c;
  }
  studentName[n] = 0;
  if (studentName[0] == 0) {
    copyName(studentName, "Connor");
  }
  if (brightnessPct < BRIGHT_MIN) {
    brightnessPct = BRIGHT_MIN;
  }
  if (brightnessPct > BRIGHT_MAX) {
    brightnessPct = BRIGHT_MAX;
  }
  // Integer division drops the remainder, so 84 becomes 80. The slider
  // only offers steps of 10, and this makes an old save match those steps.
  brightnessPct = (brightnessPct / 10) * 10;
  if (gradeLevel < 0 || gradeLevel >= GRADE_COUNT) {
    gradeLevel = GRADE_LIVE;
  }
}

// Write the three settings. false on begin() means writing is allowed.
// "clear" is the folder name inside the chip's flash. It must stay
// the same or loadSettings will not find the older values.
static void saveSettings() {
  prefs.begin("clear", false);
  prefs.putString("name", studentName);
  prefs.putInt("bright", brightnessPct);
  prefs.putInt("grade", gradeLevel);
  prefs.end();
}

// Copy text into one of the fixed radio fields and always end with a 0.
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

// The radio task calls this when a packet arrives. It only stores the
// packet. Drawing and the screen stay in loop(), where they are safe.
static void takeLinkPacket(const uint8_t *data, int len) {
  if (len == (int)sizeof(ClearDrawingResult)) {
    ClearDrawingResult result;
    memcpy(&result, data, sizeof(result));
    if (result.version != CLEAR_LINK_VERSION ||
        result.kind != CLEAR_KIND_DRAWING_RESULT) {
      return;
    }
    noInterrupts();
    pendingDrawingResult = result;
    drawingResultReady = true;
    interrupts();
    return;
  }
  if (len != (int)sizeof(ClearNote)) {
    return;
  }
  noInterrupts();
  memcpy(&pendingReply, data, sizeof(ClearNote));
  pendingReply.student[sizeof(pendingReply.student) - 1] = 0;
  pendingReply.feeling[sizeof(pendingReply.feeling) - 1] = 0;
  pendingReply.choice[sizeof(pendingReply.choice) - 1] = 0;
  replyReady = true;
  interrupts();
}

// Neither board has a clock battery. This panel starts at the time the
// sketch was uploaded, then saves the time every minute. A reboot
// continues from the last save. Time while the panel is unplugged is lost.
static const char *CLOCK_TZ = "MST7MDT,M3.2.0,M11.1.0";

static time_t compiledLocal() {
  char month[4] = {0, 0, 0, 0};
  int day = 1;
  int year = 2026;
  int hour = 0;
  int minute = 0;
  int second = 0;
  sscanf(__DATE__, "%3s %d %d", month, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int mon = 0;
  for (int i = 0; i < 12; i++) {
    if (month[0] == months[i * 3] && month[1] == months[i * 3 + 1] &&
        month[2] == months[i * 3 + 2]) {
      mon = i;
    }
  }
  struct tm stamp = {};
  stamp.tm_year = year - 1900;
  stamp.tm_mon = mon;
  stamp.tm_mday = day;
  stamp.tm_hour = hour;
  stamp.tm_min = minute;
  stamp.tm_sec = second;
  stamp.tm_isdst = -1;
  return mktime(&stamp);
}

static void saveClock() {
  time_t now = time(nullptr);
  prefs.begin("clear", false);
  prefs.putUInt("epoch", (uint32_t)now);
  prefs.end();
}

static void startClock() {
  setenv("TZ", CLOCK_TZ, 1);
  tzset();
  time_t built = compiledLocal();
  prefs.begin("clear", true);
  uint32_t saved = prefs.getUInt("epoch", 0);
  prefs.end();
  time_t start = ((time_t)saved > built) ? (time_t)saved : built;
  struct timeval tv;
  tv.tv_sec = start;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  clockMs = millis();
}

static void onTeacherReply(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t len, bool isNotify) {
  (void)characteristic;
  (void)isNotify;
  takeLinkPacket(data, (int)len);
}

class TeacherClientCallbacks : public BLEClientCallbacks {
  void onDisconnect(BLEClient *client) override {
    (void)client;
    noticeRemote = nullptr;
    replyRemote = nullptr;
  }
};

static TeacherClientCallbacks teacherClientCallbacks;
static void sendDrawingStep();

// The radio calls this when the one-second search ends. It only sets a
// flag. Connecting here would freeze the touch read.
static void onTeacherScanDone(BLEScanResults results) {
  (void)results;
  scanDone = true;
}

// Open the link to a teacher that the search already found.
static bool connectFoundTeacher(BLEAdvertisedDevice &found) {
  if (teacherClient != nullptr && teacherClient->isConnected()) {
    return noticeRemote != nullptr && replyRemote != nullptr;
  }
  if (teacherClient == nullptr) {
    teacherClient = BLEDevice::createClient();
    teacherClient->setClientCallbacks(&teacherClientCallbacks);
  }
  if (!teacherClient->connect(&found)) {
    return false;
  }
  teacherClient->setMTU(CLEAR_BLE_MTU);
  BLERemoteService *service = teacherClient->getService(CLEAR_SERVICE_UUID);
  if (service == nullptr) {
    teacherClient->disconnect();
    return false;
  }
  noticeRemote = service->getCharacteristic(CLEAR_NOTICE_UUID);
  replyRemote = service->getCharacteristic(CLEAR_REPLY_UUID);
  if (noticeRemote == nullptr || replyRemote == nullptr) {
    teacherClient->disconnect();
    return false;
  }
  if (replyRemote->canNotify()) {
    replyRemote->registerForNotify(onTeacherReply);
  }
  String current = replyRemote->readValue();
  if (current.length() == sizeof(ClearNote)) {
    takeLinkPacket((const uint8_t *)current.c_str(), (int)current.length());
  }
  return true;
}

static void sendOutbound() {
  if (noticeRemote == nullptr) {
    return;
  }
  noticeSent = noticeRemote->writeValue((uint8_t *)&outboundNote, sizeof(outboundNote), true);
  if (noticeSent) {
    needSend = false;
  } else {
    nextBleMs = millis() + 8000;
  }
  if (currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_NAME &&
      currentScreen != SCREEN_DRAW_MENU && currentScreen != SCREEN_DRAW_CANVAS) {
    paintStatus();
  }
}

// Mark the footer when a notice could not go out.
static void showSendFailed() {
  noticeSent = false;
  if (currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_NAME &&
      currentScreen != SCREEN_DRAW_MENU && currentScreen != SCREEN_DRAW_CANVAS) {
    paintStatus();
  }
}

// Look for the teacher only when a notice still has to be sent.
// The search used to freeze this sketch for a second. The touch chip
// then kept reporting the old finger, and every later tap was ignored.
// The search now finishes in the background.
static void blePump() {
  if (!linkReady || (!needSend && !drawingSyncPending)) {
    return;
  }
  if (teacherClient != nullptr && teacherClient->isConnected() && noticeRemote != nullptr) {
    if (millis() >= nextBleMs) {
      if (needSend) {
        sendOutbound();
      } else {
        sendDrawingStep();
      }
    }
    return;
  }
  if (scanDone) {
    scanDone = false;
    scanBusy = false;
    BLEScan *scan = BLEDevice::getScan();
    BLEScanResults *results = scan->getResults();
    BLEAdvertisedDevice found;
    bool haveFound = false;
    if (results != nullptr) {
      for (int i = 0; i < results->getCount(); i++) {
        BLEAdvertisedDevice device = results->getDevice(i);
        if (device.haveName() && String(device.getName().c_str()) == CLEAR_BLE_NAME) {
          found = device;
          haveFound = true;
          break;
        }
      }
    }
    scan->clearResults();
    if (!haveFound || !connectFoundTeacher(found)) {
      nextBleMs = millis() + 8000;
      showSendFailed();
      return;
    }
    if (needSend) {
      sendOutbound();
    } else if (drawingSyncPending) {
      sendDrawingStep();
    }
    return;
  }
  if (scanBusy) {
    // The finished flag can be missed if the radio resets. Stop the
    // search so a later notice can try again.
    if (!scanDone && (millis() - scanStartMs) > 3000) {
      BLEDevice::getScan()->stop();
      scanDone = true;
    }
    return;
  }
  if (millis() < nextBleMs) {
    return;
  }
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scanStartMs = millis();
  scanBusy = scan->start(1, onTeacherScanDone, false);
  if (!scanBusy) {
    nextBleMs = millis() + 8000;
  }
}

static void startTeacherLink() {
  startClock();
  BLEDevice::init("");
  BLEDevice::setMTU(CLEAR_BLE_MTU);
  linkReady = true;
}

// Redraw only the strip along the bottom. It says whether the last
// selection was sent, and then Yes or No when the teacher answers.
static void paintStatus() {
  lcd.fillRect(0, 420, 800, 60, COL_FOOT);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_FOOT);
  lcd.setCursor(24, 440);
  if (teacherReply == CLEAR_ANSWER_YES) {
    lcd.print("Teacher said yes");
  } else if (teacherReply == CLEAR_ANSWER_NO) {
    lcd.print("Teacher said no");
  } else if (noticeSent) {
    lcd.print("Sent to the teacher");
  } else if (waitingId != 0) {
    lcd.print("Could not send");
  } else if (!linkReady) {
    lcd.print("Teacher not found");
  } else {
    lcd.print("Ready");
  }
}

// Orange means the teacher is being asked to answer.
// Green means the student is trying something, and the teacher is told.
static bool itemAsks(const Item *item) {
  return item->action == ACT_ASK || item->action == ACT_TYPE;
}

// K sees the short word. Every other grade sees the full label.
// The teacher notice always uses the full label.
static const char *buttonLabel(const Item *item) {
  if (gradeLevel == GRADE_K && item->little != nullptr) {
    return item->little;
  }
  return item->label;
}

// The letters currently being typed, for whichever keyboard is open.
static char *activeText() {
  if (typeMode == TYPE_NAME) {
    return nameEdit;
  }
  return messageEdit;
}

// How many letters fit. A message with a lead has to leave room
// for those words inside the 39 characters the teacher note holds.
static int typeLimit() {
  if (typeMode == TYPE_NAME) {
    return STUDENT_NAME_MAX;
  }
  if (typeMode == TYPE_WRITE) {
    return WRITE_MAX;
  }
  int lead = 0;
  if (openItem != nullptr && openItem->lead != nullptr) {
    lead = strlen(openItem->lead);
  }
  int room = MESSAGE_MAX - lead;
  if (room < 1) {
    room = 1;
  }
  return room;
}

// The first page is the three big choices. Later lists stay in the
// normal band so six or seven buttons can still fit.
static bool homeList() {
  return crumbTop == 0 && viewCount == MAIN_COUNT;
}

static int listTop() {
  return homeList() ? 56 : LIST_TOP;
}

static int listBottom() {
  return homeList() ? 414 : LIST_BOTTOM;
}

// Share the list area evenly across however many buttons there are.
// The first page has no cap, so its three boxes fill the space under
// the title and above the status line, about four fifths of the panel.
// Later lists keep a cap so the big letters still have room.
static int rowHeightFor(int count) {
  if (count < 1) {
    return 64;
  }
  int gaps = (count > 1) ? (count - 1) * ROW_GAP : 0;
  int height = (listBottom() - listTop() - gaps) / count;
  if (homeList()) {
    return height;
  }
  int cap = 64;
  if (gradeLevel == GRADE_K) {
    cap = 72;
  } else if (gradeLevel == GRADE_12) {
    cap = 96;
  } else if (gradeLevel == GRADE_5) {
    cap = 78;
  }
  if (height > cap) {
    height = cap;
  }
  return height;
}

// Pixel y of button number index. count matters because the buttons
// get shorter when there are more of them.
static int rowTopFor(int index, int count) {
  return listTop() + index * (rowHeightFor(count) + ROW_GAP);
}

// True when the touch point is inside a rectangle. The right and
// bottom edges are not included, so two buttons that meet cannot
// both claim the same pixel.
static bool inBox(int32_t x, int32_t y, int boxX, int boxY, int boxW, int boxH) {
  return x >= boxX && x < boxX + boxW && y >= boxY && y < boxY + boxH;
}

// A rounded rectangle with its label centered. Used for Back, Done,
// Save, Change, and every keyboard key.
static void paintTopButton(int x, int y, int w, int h, const char *label, uint16_t fill) {
  lcd.fillRoundRect(x, y, w, h, 8, fill);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(COL_FG, fill);
  lcd.drawString(label, x + w / 2, y + h / 2);
}

static void paintBackButton() {
  paintTopButton(BACK_X, BACK_Y, BACK_W, BACK_H, "Back", COL_ROW);
}

// A simple face for the greeting. which is only used to pick a smile.
static void drawFeelingFace(int which, int cx, int cy) {
  lcd.fillCircle(cx, cy, 26, COL_FG);
  lcd.fillCircle(cx, cy, 22, COL_BG);
  lcd.fillCircle(cx - 8, cy - 4, 3, COL_FG);
  lcd.fillCircle(cx + 8, cy - 4, 3, COL_FG);
  if (which == 1) {
    // Bored: cover the top of each eye so they look half closed.
    lcd.fillRect(cx - 12, cy - 10, 9, 5, COL_BG);
    lcd.fillRect(cx + 4, cy - 10, 9, 5, COL_BG);
  }
  if (which == 0) {
    lcd.drawLine(cx - 14, cy - 10, cx - 3, cy - 16, COL_FG);
    lcd.drawLine(cx + 14, cy - 10, cx + 3, cy - 16, COL_FG);
  } else if (which == 3) {
    lcd.drawLine(cx - 14, cy - 16, cx - 3, cy - 10, COL_FG);
    lcd.drawLine(cx + 14, cy - 16, cx + 3, cy - 10, COL_FG);
  }
  if (which == 1) {
    lcd.drawLine(cx - 8, cy + 8, cx + 8, cy + 8, COL_FG);
  } else if (which == 2) {
    lcd.drawCircle(cx, cy + 9, 4, COL_FG);
  } else if (which == 3 || which == 4) {
    lcd.drawLine(cx - 8, cy + 12, cx, cy + 7, COL_FG);
    lcd.drawLine(cx, cy + 7, cx + 8, cy + 12, COL_FG);
  } else {
    lcd.drawLine(cx - 8, cy + 6, cx, cy + 11, COL_FG);
    lcd.drawLine(cx, cy + 11, cx + 8, cy + 6, COL_FG);
  }
}

// True when two short words are the same. Used to pick a picture.
static bool sameWord(const char *a, const char *b) {
  return a != nullptr && strcmp(a, b) == 0;
}

// A face drawn from circles and lines. mood picks the mouth and brows:
// 0 smile, 1 worried, 2 mad, 3 upset, 4 distracted, 5 uneasy.
static void drawMoodFace(int cx, int cy, int r, int mood, uint16_t ink, uint16_t paper) {
  if (r < 8) {
    r = 8;
  }
  lcd.fillCircle(cx, cy, r, ink);
  lcd.fillCircle(cx, cy, r - 2, paper);
  int eye = r / 5;
  if (eye < 2) {
    eye = 2;
  }
  int ey = cy - r / 6;
  lcd.fillCircle(cx - r / 3, ey, eye, ink);
  lcd.fillCircle(cx + r / 3, ey, eye, ink);
  if (mood == 1) {
    lcd.drawLine(cx - r / 2, ey - eye, cx - r / 6, ey - eye - r / 4, ink);
    lcd.drawLine(cx + r / 2, ey - eye, cx + r / 6, ey - eye - r / 4, ink);
  } else if (mood == 2) {
    lcd.drawLine(cx - r / 2, ey - eye - r / 4, cx - r / 6, ey - eye, ink);
    lcd.drawLine(cx + r / 2, ey - eye - r / 4, cx + r / 6, ey - eye, ink);
  } else if (mood == 4) {
    lcd.fillRect(cx - r / 3 - eye, ey - eye, eye * 2, eye, paper);
    lcd.fillRect(cx + r / 3 - eye, ey - eye, eye * 2, eye, paper);
  }
  int my = cy + r / 3;
  if (mood == 2 || mood == 3) {
    lcd.drawLine(cx - r / 3, my, cx, my - r / 5, ink);
    lcd.drawLine(cx, my - r / 5, cx + r / 3, my, ink);
  } else if (mood == 4 || mood == 5) {
    lcd.drawLine(cx - r / 3, my, cx + r / 3, my, ink);
  } else {
    lcd.drawLine(cx - r / 3, my - r / 6, cx, my, ink);
    lcd.drawLine(cx, my, cx + r / 3, my - r / 6, ink);
  }
}

// One small picture for a kindergarten or grades 1-2 button.
// key is the short word already stored on that button. The words
// the student reads are drawn separately and are not changed here.
static void drawYoungIcon(const char *key, int cx, int cy, int r, uint16_t ink, uint16_t paper) {
  if (r < 10) {
    r = 10;
  }
  if (sameWord(key, "Worried")) {
    drawMoodFace(cx, cy, r, 1, ink, paper);
  } else if (sameWord(key, "Mad")) {
    drawMoodFace(cx, cy, r, 2, ink, paper);
  } else if (sameWord(key, "Upset")) {
    drawMoodFace(cx, cy, r, 3, ink, paper);
  } else if (sameWord(key, "Busy")) {
    drawMoodFace(cx, cy, r, 4, ink, paper);
  } else if (sameWord(key, "Uneasy")) {
    drawMoodFace(cx, cy, r, 5, ink, paper);
  } else if (sameWord(key, "Kind") || sameWord(key, "COPE")) {
    drawMoodFace(cx, cy, r, 0, ink, paper);
  } else if (sameWord(key, "Ask") || sameWord(key, "Again") || sameWord(key, "Explain") ||
             sameWord(key, "Part") || sameWord(key, "Which")) {
    lcd.drawCircle(cx, cy, r, ink);
    lcd.drawCircle(cx, cy, r - 1, ink);
    lcd.setFont(r >= 18 ? &fonts::Font4 : &fonts::Font2);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(ink, paper);
    lcd.drawString("?", cx, cy + 1);
  } else if (sameWord(key, "Break") || sameWord(key, "Quiet")) {
    lcd.fillRoundRect(cx - r, cy - r / 3, r * 2, r / 2, 3, ink);
    lcd.fillRect(cx - r + 2, cy, 3, r / 2, ink);
    lcd.fillRect(cx + r - 5, cy, 3, r / 2, ink);
  } else if (sameWord(key, "Help") || sameWord(key, "Work") || sameWord(key, "Friend") || sameWord(key, "Hand")) {
    lcd.fillCircle(cx, cy + r / 4, r / 2, ink);
    lcd.fillRect(cx - r / 2, cy - r, r / 4, r, ink);
    lcd.fillRect(cx - r / 8, cy - r, r / 4, r, ink);
    lcd.fillRect(cx + r / 4, cy - r / 2, r / 4, r / 2, ink);
  } else if (sameWord(key, "Breathe")) {
    lcd.drawRect(cx - r, cy - r, r * 2, r * 2, ink);
    lcd.drawRect(cx - r + 2, cy - r + 2, r * 2 - 4, r * 2 - 4, ink);
  } else if (sameWord(key, "Walk")) {
    lcd.fillEllipse(cx - r / 3, cy - r / 5, r / 3, r / 2, ink);
    lcd.fillEllipse(cx + r / 3, cy + r / 5, r / 3, r / 2, ink);
  } else if (sameWord(key, "Talk") || sameWord(key, "Private") || sameWord(key, "Someone")) {
    lcd.fillRoundRect(cx - r, cy - r, r * 2, r, 4, ink);
    lcd.fillTriangle(cx - r / 5, cy, cx + r / 5, cy, cx - r / 2, cy + r, ink);
  } else if (sameWord(key, "Write") || sameWord(key, "Message")) {
    lcd.fillRect(cx - r / 5, cy - r, r / 2, r + r / 2, ink);
    lcd.fillTriangle(cx - r / 5, cy + r / 2, cx + r / 3, cy + r / 2, cx, cy + r, paper);
  } else if (sameWord(key, "Time")) {
    lcd.drawCircle(cx, cy, r, ink);
    lcd.drawCircle(cx, cy, r - 1, ink);
    lcd.drawLine(cx, cy, cx, cy - r / 2, ink);
    lcd.drawLine(cx, cy, cx + r / 3, cy, ink);
  } else if (sameWord(key, "Worry")) {
    lcd.fillCircle(cx - r / 3, cy, r / 2, ink);
    lcd.fillCircle(cx + r / 3, cy, r / 2, ink);
    lcd.fillCircle(cx, cy - r / 3, r / 2, ink);
  } else if (sameWord(key, "Fidget")) {
    lcd.drawCircle(cx, cy, r, ink);
    lcd.drawCircle(cx, cy, r / 2, ink);
  } else if (sameWord(key, "Example")) {
    lcd.drawRect(cx - r, cy - r / 2, r * 2, r, ink);
    lcd.drawLine(cx - r / 2, cy, cx + r / 2, cy, ink);
  } else if (sameWord(key, "Me")) {
    lcd.fillCircle(cx, cy - r / 2, r / 3, ink);
    lcd.drawLine(cx, cy - r / 6, cx, cy + r / 2, ink);
    lcd.drawLine(cx - r / 2, cy, cx + r / 2, cy, ink);
    lcd.drawLine(cx, cy + r / 2, cx - r / 3, cy + r, ink);
    lcd.drawLine(cx, cy + r / 2, cx + r / 3, cy + r, ink);
  } else {
    drawMoodFace(cx, cy, r, 0, ink, paper);
  }
}

// Draw a gear from a circle, eight teeth, and a hole in the middle.
// The font has no gear character, so the icon is built from shapes.
// The whole button, not just the icon, is what a tap has to hit.
static void paintGearButton() {
  lcd.fillRoundRect(GEAR_X, GEAR_Y, GEAR_W, GEAR_H, 8, COL_ROW);
  int cx = GEAR_X + GEAR_W / 2;
  int cy = GEAR_Y + GEAR_H / 2;
  lcd.fillCircle(cx, cy, 8, COL_FG);
  lcd.fillRect(cx - 2, cy - 14, 4, 6, COL_FG);
  lcd.fillRect(cx - 2, cy + 8, 4, 6, COL_FG);
  lcd.fillRect(cx - 14, cy - 2, 6, 4, COL_FG);
  lcd.fillRect(cx + 8, cy - 2, 6, 4, COL_FG);
  lcd.fillRect(cx - 11, cy - 11, 5, 5, COL_FG);
  lcd.fillRect(cx + 6, cy - 11, 5, 5, COL_FG);
  lcd.fillRect(cx - 11, cy + 6, 5, 5, COL_FG);
  lcd.fillRect(cx + 6, cy + 6, 5, 5, COL_FG);
  lcd.fillCircle(cx, cy, 3, COL_ROW);
}

// =============================================================================
// =============================================================================
//
//   EDIT HOW THE SCREENS ARE DRAWN HERE
//
//   Each function below draws one full page:
//     paintHello()       startup greeting
//     paintList()        the choice buttons on screen
//     paintSelected()    the steps for one choice
//     paintBreathMotion() the counting square
//     paintWrite()       three lines for how the student feels
//     paintDrawingMenu() board choices
//     paintDrawingCanvas() board surface and drawing controls
//     paintSettings()    name, brightness, and grade level
//     paintNameEditor()  the keyboard
//   The words themselves are in the text-and-colors block above.
//   The age group picked in settings changes the type size.
//   Back, the gear, and the bottom status strip are drawn by the
//   smaller functions just above this banner.
//
// =============================================================================
// =============================================================================

// Greeting. A tap anywhere except the gear leaves this screen early.
// After HELLO_MS with no tap, loop() leaves it on its own.
static void paintHello() {
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(COL_FG, COL_BG);
  char greeting[STUDENT_NAME_MAX + 16];
  snprintf(greeting, sizeof(greeting), "Hello, %s", studentName);
  if (gradeLevel == GRADE_K || gradeLevel == GRADE_12) {
    lcd.setFont(&fonts::Font4);
    lcd.drawString(greeting, 400, 150);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("What do you need?", 400, 210);
    if (gradeLevel == GRADE_K) {
      drawFeelingFace(5, 400, 300);
    }
  } else {
    lcd.setFont(&fonts::Font4);
    lcd.drawString(greeting, 400, 170);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("What do you need?", 400, 230);
  }
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// Big type when the rows are tall. Small type when many rows share
// the screen, and for grade 5.
static void listFont(int height) {
  if (gradeLevel == GRADE_5 || height < 56) {
    lcd.setFont(&fonts::Font2);
  } else {
    lcd.setFont(&fonts::Font4);
  }
}

// The list the student is in. The first list has no Back button.
// Orange rows ask the teacher. Green rows are something to try.
// A plain row only opens the next list.
static void paintList() {
  int height = rowHeightFor(viewCount);
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(COL_FG, COL_BG);
  listFont(80);
  lcd.drawString(viewTitle, 24, 32);
  if (crumbTop > 0) {
    paintBackButton();
  }
  for (int i = 0; i < viewCount; i++) {
    const Item *item = &viewItems[i];
    int top = rowTopFor(i, viewCount);
    uint16_t fill = COL_ROW;
    if (item->action != ACT_OPEN) {
      fill = itemAsks(item) ? COL_ASK : COL_TRY;
    }
    lcd.fillRoundRect(ROW_X, top, ROW_W, height, 12, fill);
    lcd.setTextColor(COL_FG, fill);
    lcd.setTextDatum(textdatum_t::middle_left);
    listFont(height);
    lcd.setTextColor(COL_FG, fill);
    int textX = ROW_X + 16;
    // K and grades 1-2 get a picture. The words stay the ones they already had.
    if (gradeLevel == GRADE_K || gradeLevel == GRADE_12) {
      int radius = height / 2 - 8;
      if (radius > 34) {
        radius = 34;
      }
      if (radius < 12) {
        radius = 12;
      }
      int iconX = ROW_X + 14 + radius;
      drawYoungIcon(item->little, iconX, top + height / 2, radius, COL_FG, fill);
      textX = iconX + radius + 14;
      listFont(height);
      lcd.setTextColor(COL_FG, fill);
    }
    lcd.drawString(buttonLabel(item), textX, top + height / 2);
    if (item->action != ACT_OPEN && gradeLevel != GRADE_K && height >= 40) {
      lcd.setTextDatum(textdatum_t::middle_right);
      lcd.setFont(&fonts::Font2);
      lcd.drawString(itemAsks(item) ? "Ask" : "Try", ROW_X + ROW_W - 16, top + height / 2);
    }
  }
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// Draw text that wraps onto new lines. A \n in the string forces a
// break even when the line is not full. Words are copied one at a time
// into buf because drawString needs a normal C string, not a slice.
static void drawWrapped(const char *text, int x, int y, int maxWidth, bool small) {
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(COL_FG, COL_BG);
  // Small type fits more words on the page. Grade 5 uses it for that.
  // K uses it for the steps so the one big word stays the focus.
  lcd.setFont(small ? &fonts::Font2 : &fonts::Font4);
  int cursorX = x;
  int cursorY = y;
  int lineHeight = lcd.fontHeight() + 8;
  const char *word = text;
  while (*word) {
    if (*word == '\n') {
      cursorX = x;
      cursorY += lineHeight;
      word++;
      continue;
    }
    while (*word == ' ') {
      word++;
    }
    if (*word == 0) {
      break;
    }
    const char *end = word;
    while (*end && *end != ' ' && *end != '\n') {
      end++;
    }
    char buf[80];
    int len = end - word;
    if (len > 79) {
      len = 79;
    }
    for (int i = 0; i < len; i++) {
      buf[i] = word[i];
    }
    buf[len] = 0;
    int width = lcd.textWidth(buf);
    // If this word would run past the right edge, and it is not the
    // first word on the line, drop it to the next line.
    if (cursorX > x && cursorX + width > x + maxWidth) {
      cursorX = x;
      cursorY += lineHeight;
    }
    lcd.drawString(buf, cursorX, cursorY);
    cursorX += width + lcd.textWidth(" ");
    word = end;
  }
}

// Where the breathing square is, and which number to show.
// Phase 0 grows, 1 holds, 2 shrinks, 3 holds. Each phase is 4 seconds.
static void breathGeometry(int *phase, int *count, int *size) {
  unsigned long into = millis() - breathMs;
  int step = (int)((into / 1000) % 4);
  *phase = (int)((into / 4000) % 4);
  *count = 4 - step;
  if (*count < 1) {
    *count = 1;
  }
  unsigned long along = into % 4000;
  if (*phase == 0) {
    *size = 48 + (int)(along * 140 / 4000);
  } else if (*phase == 1) {
    *size = 188;
  } else if (*phase == 2) {
    *size = 188 - (int)(along * 140 / 4000);
  } else {
    *size = 48;
  }
}

// Redraw only the middle of the breathing screen, so Back and the
// footer do not flicker while the square and the count change.
static void paintBreathMotion() {
  static const char *words[] = {"Breathe in", "Hold", "Breathe out", "Hold"};
  int phase = 0;
  int count = 4;
  int size = 48;
  breathGeometry(&phase, &count, &size);
  lcd.fillRect(80, 140, 640, 260, COL_BG);
  int x = 400 - size / 2;
  int y = 300 - size / 2;
  lcd.drawRect(x, y, size, size, COL_TRY);
  lcd.drawRect(x + 2, y + 2, size - 4, size - 4, COL_TRY);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.setFont(&fonts::Font4);
  char num[4];
  snprintf(num, sizeof(num), "%d", count);
  lcd.drawString(num, 400, 300);
  lcd.setFont(&fonts::Font2);
  lcd.drawString(words[phase], 400, 160);
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintBeginButton() {
  paintTopButton(24, 336, 752, 60, "Begin", COL_TRY);
}

// The detail page for one choice. Box breathing shows Begin until
// the student starts the count.
static void paintSelected() {
  if (openItem == nullptr) {
    paintList();
    return;
  }
  if (openItem->action == ACT_BREATHE && breathRunning) {
    lcd.fillScreen(COL_BG);
    paintBackButton();
    paintBreathMotion();
    paintStatus();
    paintGearButton();
    lcd.setTextDatum(textdatum_t::top_left);
    return;
  }
  uint16_t band = itemAsks(openItem) ? COL_ASK : COL_TRY;
  const char *banner = itemAsks(openItem) ? "Ask my teacher" : "Try this";
  if (gradeLevel == GRADE_K) {
    banner = itemAsks(openItem) ? "Ask" : "Try";
  }
  lcd.fillScreen(COL_BG);
  paintBackButton();
  lcd.fillRoundRect(24, 64, 752, 64, 12, band);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(gradeLevel == GRADE_5 ? &fonts::Font2 : &fonts::Font4);
  lcd.setTextColor(COL_FG, band);
  lcd.drawString(banner, 44, 96);
  lcd.setTextColor(COL_FG, COL_BG);
  bool smallWords = (gradeLevel == GRADE_5 || gradeLevel == GRADE_K);
  drawWrapped(openItem->label, 24, 148, 752, smallWords);
  if (openItem->help != nullptr) {
    drawWrapped(openItem->help, 24, 200, 752, true);
  }
  if (openItem->action == ACT_BREATHE) {
    paintBeginButton();
  }
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// Three lines the student can fill. Done tells the teacher they wrote,
// without sending the words themselves.
static void paintWrite() {
  lcd.fillScreen(COL_BG);
  paintBackButton();
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Done", COL_TRY);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Write how you feel", 24, 32);
  for (int i = 0; i < 3; i++) {
    int top = 80 + i * 78;
    lcd.fillRoundRect(ROW_X, top, ROW_W, 68, 12, COL_ROW);
    lcd.setTextColor(COL_FG, COL_ROW);
    lcd.setFont(&fonts::Font4);
    char mark[4];
    snprintf(mark, sizeof(mark), "%d", i + 1);
    lcd.drawString(mark, ROW_X + 16, top + 34);
    lcd.setFont(&fonts::Font2);
    if (writeLines[i][0] == 0) {
      lcd.drawString("Tap to write", ROW_X + 56, top + 34);
    } else {
      lcd.drawString(writeLines[i], ROW_X + 56, top + 34);
    }
  }
  paintStatus();
  lcd.setTextDatum(textdatum_t::top_left);
}

static int gradeRowTop(int index) {
  return GRADE_TOP + index * (GRADE_H + GRADE_GAP);
}

// Settings page. Brightness and age group save as soon as they change.
// The name is only saved from the keyboard screen.
// The green row is the look students see on the other screens.
static void paintSettings() {
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Settings", 24, 30);
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Done", COL_TRY);

  lcd.fillRoundRect(ROW_X, NAME_Y, ROW_W, NAME_H, 12, COL_ROW);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.drawString("Student", 44, NAME_Y + NAME_H / 2);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(studentName, 160, NAME_Y + NAME_H / 2);
  paintTopButton(CHANGE_X, CHANGE_Y, CHANGE_W, CHANGE_H, "Change", COL_TRY);

  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Brightness", 24, BRIGHT_LABEL_Y);
  paintTopButton(MINUS_X, BRIGHT_Y, STEP_W, BRIGHT_H, "-", COL_ROW);
  paintTopButton(PLUS_X, BRIGHT_Y, STEP_W, BRIGHT_H, "+", COL_ROW);
  lcd.fillRoundRect(BAR_X, BRIGHT_Y + 8, BAR_W, BRIGHT_H - 16, 8, COL_FOOT);
  // How much of the bar to fill. At 20% the fill is empty. At 100% it is full.
  int fillW = BAR_W * (brightnessPct - BRIGHT_MIN) / (BRIGHT_MAX - BRIGHT_MIN);
  if (fillW < 0) {
    fillW = 0;
  }
  if (fillW > BAR_W) {
    fillW = BAR_W;
  }
  if (fillW > 0) {
    lcd.fillRoundRect(BAR_X, BRIGHT_Y + 8, fillW, BRIGHT_H - 16, 8, COL_TRY);
  }
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", brightnessPct);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString(pct, PCT_X + PCT_W / 2, BRIGHT_Y + BRIGHT_H / 2);

  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Age group", 24, GRADE_NOTE_Y);
  lcd.setTextDatum(textdatum_t::top_right);
  lcd.drawString("Students see this look now.", 776, GRADE_NOTE_Y);

  for (int i = 0; i < GRADE_COUNT; i++) {
    int top = gradeRowTop(i);
    bool selected = (i == gradeLevel);
    uint16_t fill = selected ? COL_TRY : COL_ROW;
    lcd.fillRoundRect(ROW_X, top, ROW_W, GRADE_H, 12, fill);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, fill);
    lcd.drawString(gradeOptions[i].label, ROW_X + 16, top + 6);
    lcd.drawString(gradeOptions[i].about, ROW_X + 16, top + 30);
    if (selected) {
      lcd.setTextDatum(textdatum_t::middle_right);
      lcd.drawString("In use", ROW_X + ROW_W - 16, top + GRADE_H / 2);
    }
  }
  lcd.setTextDatum(textdatum_t::top_left);
}

// Add one key to the list, unless the list is already full.
// Returns the new count so the caller can keep appending.
static int pushKey(KeyHit *out, int n, int action, char ch, int x, int y, int w, int h) {
  if (n >= KEY_MAX) {
    return n;
  }
  out[n].action = action;
  out[n].ch = ch;
  out[n].x = x;
  out[n].y = y;
  out[n].w = w;
  out[n].h = h;
  return n + 1;
}

// Build the keyboard rectangles. Painting and tap testing both call
// this, so a key cannot be drawn in one place and detected in another.
// The third letter row leaves an eighth slot for Delete.
static int collectKeys(KeyHit *out) {
  int n = 0;
  const char *rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  const int counts[] = {10, 9, 7};
  for (int r = 0; r < 3; r++) {
    int slots = (r == 2) ? 8 : counts[r];
    int keyW = (KEY_AREA_W - (slots - 1) * KEY_GAP) / slots;
    int rowW = slots * keyW + (slots - 1) * KEY_GAP;
    int x = KEY_LEFT + (KEY_AREA_W - rowW) / 2;
    int y = KEY_TOP + r * (KEY_H + KEY_GAP);
    for (int c = 0; c < counts[r]; c++) {
      n = pushKey(out, n, KEY_CHAR, rows[r][c], x, y, keyW, KEY_H);
      x += keyW + KEY_GAP;
    }
    if (r == 2) {
      n = pushKey(out, n, KEY_BACKSPACE, 0, x, y, keyW, KEY_H);
    }
  }

  int y = KEY_TOP + 3 * (KEY_H + KEY_GAP);
  int markW = 80;
  int clearW = 160;
  int spaceW = KEY_AREA_W - markW - markW - clearW - 3 * KEY_GAP;
  int x = KEY_LEFT;
  n = pushKey(out, n, KEY_CHAR, '\'', x, y, markW, KEY_H);
  x += markW + KEY_GAP;
  n = pushKey(out, n, KEY_CHAR, '-', x, y, markW, KEY_H);
  x += markW + KEY_GAP;
  n = pushKey(out, n, KEY_SPACE, ' ', x, y, spaceW, KEY_H);
  x += spaceW + KEY_GAP;
  n = pushKey(out, n, KEY_CLEAR, 0, x, y, clearW, KEY_H);
  return n;
}

// The word or letter drawn on a key. Letter keys show the uppercase
// character. The name itself is stored in normal title case.
static void keyLabel(const KeyHit *key, char *buf, int len) {
  if (key->action == KEY_BACKSPACE) {
    snprintf(buf, len, "Del");
  } else if (key->action == KEY_CLEAR) {
    snprintf(buf, len, "Clear");
  } else if (key->action == KEY_SPACE) {
    snprintf(buf, len, "Space");
  } else {
    buf[0] = key->ch;
    buf[1] = 0;
  }
}

// Keyboard page. Save is green only when the letters are not empty.
// A student message says Send. The name keyboard says Save.
// Back throws away what was typed.
static void paintNameEditor() {
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  const char *title = "Student name";
  const char *action = "Save";
  if (typeMode == TYPE_MESSAGE && openItem != nullptr) {
    title = openItem->label;
    action = "Send";
  } else if (typeMode == TYPE_WRITE) {
    title = "Write a line";
    action = "Save";
  }
  lcd.drawString(title, 24, 30);
  paintTopButton(NAME_BACK_X, NAME_BACK_Y, NAME_BACK_W, NAME_BACK_H, "Back", COL_ROW);
  char *text = activeText();
  uint16_t saveFill = text[0] ? COL_TRY : COL_ROW;
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, action, saveFill);

  lcd.fillRoundRect(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, 12, COL_ROW);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_ROW);
  int midY = FIELD_Y + FIELD_H / 2;
  if (text[0] == 0) {
    lcd.setFont(&fonts::Font2);
    lcd.drawString("Tap letters below", FIELD_X + 20, midY);
  } else {
    lcd.drawString(text, FIELD_X + 20, midY);
    int textW = lcd.textWidth(text);
    // A thin cursor just after the letters, so it is obvious where the next one goes.
    lcd.fillRect(FIELD_X + 26 + textW, FIELD_Y + 18, 3, 28, COL_FG);
  }

  KeyHit keys[KEY_MAX];
  int count = collectKeys(keys);
  for (int i = 0; i < count; i++) {
    uint16_t fill = COL_ROW;
    if (keys[i].action == KEY_BACKSPACE || keys[i].action == KEY_CLEAR) {
      fill = COL_FOOT;
    }
    char label[8];
    keyLabel(&keys[i], label, sizeof(label));
    paintTopButton(keys[i].x, keys[i].y, keys[i].w, keys[i].h, label, fill);
  }
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintDrawingMenu() {
  lcd.fillScreen(COL_BG);
  paintTopButton(DRAW_BACK_X, DRAW_BACK_Y, DRAW_BACK_W, DRAW_BACK_H, "Back", COL_ROW);

  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Graph board, White Board, or Chalk board?", 400, 30);

  const int optionX = 24;
  const int optionW = 752;
  const int optionH = 96;
  const int optionYs[] = {82, 190, 298};
  const uint16_t fills[] = {0xFFFF, 0xFFFF, 0x13A4};
  const char *labels[] = {"Graph Board - Digital Pencil",
                          "White Board - Digital Marker",
                          "Chalk Board - Digital Chalk"};
  const uint16_t textColors[] = {COL_FG, COL_FG, 0xFFFF};

  for (int i = 0; i < 3; i++) {
    lcd.fillRoundRect(optionX, optionYs[i], optionW, optionH, 12, fills[i]);
    lcd.drawRoundRect(optionX, optionYs[i], optionW, optionH, 12, COL_ROW);
    lcd.setTextColor(textColors[i], fills[i]);
    lcd.setFont(&fonts::Font4);
    lcd.drawString(labels[i], 400, optionYs[i] + optionH / 2);
  }
  lcd.setTextDatum(textdatum_t::top_left);
}

static uint16_t drawingSurfaceColor() {
  return selectedBoard == BOARD_CHALK ? 0x13A4 : 0xFFFF;
}

static void paintDrawingCanvas() {
  uint16_t surface = drawingSurfaceColor();
  uint16_t header = selectedBoard == BOARD_CHALK ? 0x13A4 : COL_BG;
  lcd.fillScreen(surface);
  lcd.fillRect(0, 0, 800, DRAW_HEADER_H, header);

  if (selectedBoard == BOARD_GRAPH) {
    const uint16_t gridColor = 0xCE79;
    for (int x = 0; x < 800; x += 20) {
      lcd.drawLine(x, DRAW_HEADER_H, x, DRAW_CANVAS_BOTTOM - 1, gridColor);
    }
    for (int y = DRAW_HEADER_H; y < DRAW_CANVAS_BOTTOM; y += 20) {
      lcd.drawLine(0, y, 799, y, gridColor);
    }
  }

  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(selectedBoard == BOARD_CHALK ? 0xFFFF : COL_FG, header);
  if (selectedBoard == BOARD_GRAPH) {
    lcd.drawString("Graph Board - Pencil", 555, 30);
  } else if (selectedBoard == BOARD_WHITE) {
    lcd.drawString("White Board - Marker", 555, 30);
  } else {
    lcd.drawString("Choose chalk", 430, 30);
  }

  uint16_t buttonFill = selectedBoard == BOARD_CHALK ? 0xFFFF : COL_ROW;
  paintTopButton(DRAW_BACK_X, DRAW_BUTTON_Y, DRAW_BACK_W, DRAW_BUTTON_H, "Back", buttonFill);
  paintTopButton(DRAW_SAVE_X, DRAW_BUTTON_Y, 80, DRAW_BUTTON_H, "Save", buttonFill);
  paintTopButton(DRAW_RESET_X, DRAW_BUTTON_Y, 104, DRAW_BUTTON_H, "Reset", buttonFill);
  paintTopButton(DRAW_NEW_X, DRAW_BUTTON_Y, 96, DRAW_BUTTON_H, "New", buttonFill);
  if (selectedBoard == BOARD_CHALK) {
    for (int i = 0; i < DRAW_COLOR_COUNT; i++) {
      lcd.fillRoundRect(DRAW_COLOR_X[i], DRAW_COLOR_Y, DRAW_COLOR_W, DRAW_COLOR_H,
                        8, DRAW_CHALK_COLORS[i]);
      lcd.drawRoundRect(DRAW_COLOR_X[i], DRAW_COLOR_Y, DRAW_COLOR_W, DRAW_COLOR_H,
                        8, i == selectedChalkColor ? COL_FG : COL_ROW);
      if (i == selectedChalkColor) {
        lcd.drawRect(DRAW_COLOR_X[i] + 2, DRAW_COLOR_Y + 2,
                     DRAW_COLOR_W - 4, DRAW_COLOR_H - 4, COL_FG);
      }
      lcd.setTextDatum(textdatum_t::middle_center);
      lcd.setFont(&fonts::Font2);
      uint16_t labelColor = (i == 1 || i == 0) ? COL_FG : 0xFFFF;
      lcd.setTextColor(labelColor, DRAW_CHALK_COLORS[i]);
      lcd.drawString(DRAW_CHALK_NAMES[i], DRAW_COLOR_X[i] + DRAW_COLOR_W / 2,
                     DRAW_COLOR_Y + DRAW_COLOR_H / 2);
    }
  }

  for (int i = 0; i < drawingSegmentCount; i++) {
    const DrawingSegment &segment = drawingSegments[i];
    int firstOffset = -(segment.width / 2);
    int lastOffset = firstOffset + segment.width - 1;
    for (int offset = firstOffset; offset <= lastOffset; offset++) {
      int y1 = segment.y1 + offset;
      int y2 = segment.y2 + offset;
      if (y1 < DRAW_HEADER_H) y1 = DRAW_HEADER_H;
      if (y2 < DRAW_HEADER_H) y2 = DRAW_HEADER_H;
      if (y1 >= DRAW_CANVAS_BOTTOM) y1 = DRAW_CANVAS_BOTTOM - 1;
      if (y2 >= DRAW_CANVAS_BOTTOM) y2 = DRAW_CANVAS_BOTTOM - 1;
      lcd.drawLine(segment.x1, y1, segment.x2, y2, segment.color);
    }
  }

  lcd.fillRect(0, DRAW_CANVAS_BOTTOM, 800, 480 - DRAW_CANVAS_BOTTOM, COL_FOOT);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_FOOT);
  lcd.drawString(drawStatus, 18, DRAW_CANVAS_BOTTOM + 30);
  lcd.setTextDatum(textdatum_t::top_left);
  cursorVisible = false;
}

static uint16_t drawingInkColor() {
  if (selectedBoard == BOARD_GRAPH) {
    return 0x2124;
  }
  return selectedBoard == BOARD_WHITE ? 0x0000 : DRAW_CHALK_COLORS[selectedChalkColor];
}

static int drawingInkWidth() {
  if (selectedBoard == BOARD_GRAPH) {
    return 2;
  }
  return selectedBoard == BOARD_WHITE ? 7 : 5;
}

static void drawCursorTool(int x, int y) {
  if (selectedBoard == BOARD_GRAPH) {
    lcd.drawLine(x, y, x + 17, y - 25, 0x8410);
    lcd.drawLine(x + 2, y - 1, x + 19, y - 26, 0x2124);
    lcd.drawLine(x + 4, y - 2, x + 21, y - 27, 0x8410);
    lcd.fillTriangle(x, y, x + 4, y - 2, x + 2, y + 3, 0xFBE0);
    lcd.drawLine(x + 2, y + 3, x + 4, y - 2, COL_FG);
  } else if (selectedBoard == BOARD_WHITE) {
    lcd.drawLine(x, y, x + 19, y - 22, 0x0000);
    lcd.drawLine(x + 2, y - 1, x + 21, y - 23, 0x0000);
    lcd.drawLine(x + 17, y - 21, x + 21, y - 23, 0xC618);
    lcd.drawLine(x + 3, y - 5, x + 7, y - 1, 0xC618);
    lcd.fillTriangle(x, y, x + 3, y - 4, x + 4, y + 3, 0xC618);
  } else {
    uint16_t chalk = drawingInkColor();
    lcd.drawLine(x, y, x + 15, y - 21, chalk);
    lcd.drawLine(x + 2, y - 1, x + 17, y - 22, chalk);
    lcd.drawLine(x + 4, y - 2, x + 19, y - 23, 0xC618);
  }
}

static bool clipDrawingLine(int &x1, int &y1, int &x2, int &y2,
                            int left, int top, int right, int bottom) {
  double dx = x2 - x1;
  double dy = y2 - y1;
  double t0 = 0.0;
  double t1 = 1.0;
  double p[4] = {-dx, dx, -dy, dy};
  double q[4] = {x1 - left, right - x1, y1 - top, bottom - y1};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0) return false;
      continue;
    }
    double ratio = q[i] / p[i];
    if (p[i] < 0.0) {
      if (ratio > t1) return false;
      if (ratio > t0) t0 = ratio;
    } else {
      if (ratio < t0) return false;
      if (ratio < t1) t1 = ratio;
    }
  }
  int originalX = x1;
  int originalY = y1;
  x1 = originalX + (int)(t0 * dx);
  y1 = originalY + (int)(t0 * dy);
  x2 = originalX + (int)(t1 * dx);
  y2 = originalY + (int)(t1 * dy);
  return true;
}

static void restoreCursorArea(int x, int y) {
  const int left = x - 2;
  const int top = y - 29;
  const int right = x + 24;
  const int bottom = y + 4;
  const int clippedLeft = left < 0 ? 0 : left;
  const int clippedTop = top < DRAW_HEADER_H ? DRAW_HEADER_H : top;
  const int clippedRight = right > 799 ? 799 : right;
  const int clippedBottom = bottom >= DRAW_CANVAS_BOTTOM ? DRAW_CANVAS_BOTTOM - 1 : bottom;
  if (clippedLeft > clippedRight || clippedTop > clippedBottom) {
    return;
  }

  lcd.fillRect(clippedLeft, clippedTop, clippedRight - clippedLeft + 1,
               clippedBottom - clippedTop + 1, drawingSurfaceColor());
  if (selectedBoard == BOARD_GRAPH) {
    for (int gx = (clippedLeft / 20) * 20; gx <= clippedRight; gx += 20) {
      lcd.drawLine(gx, clippedTop, gx, clippedBottom, 0xCE79);
    }
    int firstGridY = DRAW_HEADER_H +
                     ((clippedTop - DRAW_HEADER_H) / 20) * 20;
    for (int gy = firstGridY; gy <= clippedBottom; gy += 20) {
      lcd.drawLine(clippedLeft, gy, clippedRight, gy, 0xCE79);
    }
  }

  for (int i = 0; i < drawingSegmentCount; i++) {
    const DrawingSegment &segment = drawingSegments[i];
    int firstOffset = -(segment.width / 2);
    int lastOffset = firstOffset + segment.width - 1;
    for (int offset = firstOffset; offset <= lastOffset; offset++) {
      int x1 = segment.x1;
      int y1 = segment.y1 + offset;
      int x2 = segment.x2;
      int y2 = segment.y2 + offset;
      if (clipDrawingLine(x1, y1, x2, y2, clippedLeft, clippedTop,
                          clippedRight, clippedBottom)) {
        lcd.drawLine(x1, y1, x2, y2, segment.color);
      }
    }
  }
}

static void moveDrawingCursor(int x, int y) {
  if (cursorVisible) {
    restoreCursorArea(cursorX, cursorY);
  }
  drawCursorTool(x, y);
  cursorX = x;
  cursorY = y;
  cursorVisible = true;
}

static void drawInkSegment(int x1, int y1, int x2, int y2) {
  if (drawingSegmentCount >= DRAW_SEGMENT_MAX) {
    copyField(drawStatus, sizeof(drawStatus), "Board has reached its drawing limit");
    drawStatusUntilMs = millis() + 3000;
    return;
  }
  DrawingSegment &segment = drawingSegments[drawingSegmentCount++];
  segment.x1 = x1;
  segment.y1 = y1;
  segment.x2 = x2;
  segment.y2 = y2;
  segment.color = drawingInkColor();
  segment.width = drawingInkWidth();

  int firstOffset = -(segment.width / 2);
  int lastOffset = firstOffset + segment.width - 1;
  for (int offset = firstOffset; offset <= lastOffset; offset++) {
    int y1Offset = y1 + offset;
    int y2Offset = y2 + offset;
    if (y1Offset < DRAW_HEADER_H) y1Offset = DRAW_HEADER_H;
    if (y2Offset < DRAW_HEADER_H) y2Offset = DRAW_HEADER_H;
    if (y1Offset >= DRAW_CANVAS_BOTTOM) y1Offset = DRAW_CANVAS_BOTTOM - 1;
    if (y2Offset >= DRAW_CANVAS_BOTTOM) y2Offset = DRAW_CANVAS_BOTTOM - 1;
    lcd.drawLine(x1, y1Offset, x2, y2Offset, segment.color);
  }
}

static const char *drawingFilePath(int board) {
  if (board == BOARD_GRAPH) return "/graph.bin";
  if (board == BOARD_WHITE) return "/white.bin";
  return "/chalk.bin";
}

static const char *drawingBoardName(int board) {
  if (board == BOARD_GRAPH) return "graph";
  if (board == BOARD_WHITE) return "whiteboard";
  return "chalkboard";
}

static void setDrawStatus(const char *text, unsigned long durationMs = 0) {
  copyField(drawStatus, sizeof(drawStatus), text);
  drawStatusUntilMs = durationMs ? millis() + durationMs : 0;
  if (currentScreen == SCREEN_DRAW_CANVAS) {
    lcd.fillRect(0, DRAW_CANVAS_BOTTOM, 800, 480 - DRAW_CANVAS_BOTTOM, COL_FOOT);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, COL_FOOT);
    lcd.drawString(drawStatus, 18, DRAW_CANVAS_BOTTOM + 30);
    lcd.setTextDatum(textdatum_t::top_left);
  }
}

static uint32_t newDrawingId() {
  uint32_t id = esp_random();
  return id ? id : 1;
}

static void warnDrawingStorage(const char *message) {
  storageSavePending = true;
  if (!storageWarningActive) {
    storageWarningActive = true;
    storageWarningStartedMs = millis();
  }
  setDrawStatus(message);
}

static bool saveDrawingFile() {
  if (!drawStorageReady) {
    setDrawStatus("Local drawing storage is unavailable");
    return false;
  }
  const char *path = drawingFilePath(selectedBoard);
  const char *tempPath = "/drawing.tmp";
  size_t bytes = sizeof(DrawingFileHeader) +
                 (size_t)drawingSegmentCount * sizeof(DrawingSegment);
  size_t freeBytes = SPIFFS.totalBytes() > SPIFFS.usedBytes()
                       ? SPIFFS.totalBytes() - SPIFFS.usedBytes() : 0;
  if (freeBytes < bytes + 256) {
    warnDrawingStorage("Storage low. Old board will be removed in 2:00");
    return false;
  }

  File file = SPIFFS.open(tempPath, FILE_WRITE);
  if (!file) {
    warnDrawingStorage("Storage issue. Old board will be removed in 2:00");
    return false;
  }
  DrawingFileHeader header = {};
  header.magic = DRAW_FILE_MAGIC;
  header.drawingId = currentDrawingId;
  header.savedAt = (uint32_t)time(nullptr);
  header.segmentCount = drawingSegmentCount;
  header.version = DRAW_FILE_VERSION;
  header.board = selectedBoard;
  header.chalkColor = selectedChalkColor;
  size_t written = file.write((const uint8_t *)&header, sizeof(header));
  if (written == sizeof(header) && drawingSegmentCount > 0) {
    written += file.write((const uint8_t *)drawingSegments,
                          (size_t)drawingSegmentCount * sizeof(DrawingSegment));
  }
  file.flush();
  file.close();
  const char *backupPath = "/drawing.bak";
  SPIFFS.remove(backupPath);
  bool hadPrevious = SPIFFS.exists(path);
  bool movedPrevious = !hadPrevious || SPIFFS.rename(path, backupPath);
  bool replaced = written == bytes && movedPrevious &&
                  SPIFFS.rename(tempPath, path);
  if (!replaced) {
    if (hadPrevious && movedPrevious) {
      SPIFFS.rename(backupPath, path);
    }
    SPIFFS.remove(tempPath);
    warnDrawingStorage("Storage issue. Old board will be removed in 2:00");
    return false;
  }
  SPIFFS.remove(backupPath);
  storageSavePending = false;
  storageWarningActive = false;
  if (drawingSegmentCount == 0) {
    setDrawStatus("Blank board saved");
  } else {
    setDrawStatus("Drawing saved on this panel");
  }
  return true;
}

static bool loadDrawingFile(int board) {
  drawingSegmentCount = 0;
  currentDrawingId = newDrawingId();
  selectedBoard = board;
  selectedChalkColor = 1;
  if (!drawStorageReady) {
    setDrawStatus("Local drawing storage is unavailable");
    return false;
  }
  File file = SPIFFS.open(drawingFilePath(board), FILE_READ);
  if (!file) {
    return true;
  }
  DrawingFileHeader header = {};
  if (file.read((uint8_t *)&header, sizeof(header)) != sizeof(header) ||
      header.magic != DRAW_FILE_MAGIC || header.version != DRAW_FILE_VERSION ||
      header.board != board || header.segmentCount > DRAW_SEGMENT_MAX ||
      file.size() != sizeof(header) +
                     (size_t)header.segmentCount * sizeof(DrawingSegment)) {
    file.close();
    SPIFFS.remove(drawingFilePath(board));
    setDrawStatus("An unreadable saved board was removed");
    return false;
  }
  if (header.segmentCount > 0 &&
      file.read((uint8_t *)drawingSegments,
                (size_t)header.segmentCount * sizeof(DrawingSegment)) !=
        (size_t)header.segmentCount * sizeof(DrawingSegment)) {
    file.close();
    SPIFFS.remove(drawingFilePath(board));
    setDrawStatus("An unreadable saved board was removed");
    return false;
  }
  file.close();
  for (int i = 0; i < header.segmentCount; i++) {
    if (drawingSegments[i].x1 > 799 || drawingSegments[i].x2 > 799 ||
        drawingSegments[i].y1 < DRAW_HEADER_H ||
        drawingSegments[i].y1 >= DRAW_CANVAS_BOTTOM ||
        drawingSegments[i].y2 < DRAW_HEADER_H ||
        drawingSegments[i].y2 >= DRAW_CANVAS_BOTTOM ||
        drawingSegments[i].width == 0 || drawingSegments[i].width > 12) {
      SPIFFS.remove(drawingFilePath(board));
      setDrawStatus("An unreadable saved board was removed");
      return false;
    }
  }
  currentDrawingId = header.drawingId ? header.drawingId : newDrawingId();
  selectedChalkColor = header.chalkColor < DRAW_COLOR_COUNT ? header.chalkColor : 1;
  drawingSegmentCount = header.segmentCount;
  return true;
}

static bool deleteOldestLocalDrawing() {
  int oldestBoard = -1;
  uint32_t oldestTime = UINT32_MAX;
  for (int board = 0; board < 3; board++) {
    if (board == selectedBoard) continue;
    File file = SPIFFS.open(drawingFilePath(board), FILE_READ);
    if (!file) continue;
    DrawingFileHeader header = {};
    bool valid = file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) &&
                 header.magic == DRAW_FILE_MAGIC && header.board == board;
    file.close();
    uint32_t savedAt = valid ? header.savedAt : 0;
    if (oldestBoard < 0 || savedAt < oldestTime) {
      oldestBoard = board;
      oldestTime = savedAt;
    }
  }
  if (oldestBoard < 0) {
    for (int board = 0; board < 3; board++) {
      File file = SPIFFS.open(drawingFilePath(board), FILE_READ);
      if (!file) continue;
      DrawingFileHeader header = {};
      bool valid = file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) &&
                   header.magic == DRAW_FILE_MAGIC && header.board == board;
      file.close();
      uint32_t savedAt = valid ? header.savedAt : 0;
      if (oldestBoard < 0 || savedAt < oldestTime) {
        oldestBoard = board;
        oldestTime = savedAt;
      }
    }
  }
  if (oldestBoard < 0 || !SPIFFS.remove(drawingFilePath(oldestBoard))) {
    return false;
  }
  char status[64];
  snprintf(status, sizeof(status), "Removed oldest local %s board",
           drawingBoardName(oldestBoard));
  setDrawStatus(status, 5000);
  return true;
}

static void writeXmlAttribute(File &file, const char *text) {
  for (const char *p = text; p != nullptr && *p; p++) {
    if (*p == '&') file.print("&amp;");
    else if (*p == '"') file.print("&quot;");
    else if (*p == '<') file.print("&lt;");
    else if (*p == '>') file.print("&gt;");
    else file.write((uint8_t)*p);
  }
}

static void colorToHex(uint16_t color, char *out, size_t size) {
  uint8_t red = ((color >> 11) & 0x1F) * 255 / 31;
  uint8_t green = ((color >> 5) & 0x3F) * 255 / 63;
  uint8_t blue = (color & 0x1F) * 255 / 31;
  snprintf(out, size, "#%02X%02X%02X", red, green, blue);
}

static bool writeDrawingSvg(int board, const char *path) {
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) return false;
  file.print("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"480\" viewBox=\"0 0 800 480\" data-board=\"");
  file.print(drawingBoardName(board));
  file.print("\" data-student=\"");
  writeXmlAttribute(file, studentName);
  file.print("\" data-drawing-id=\"");
  file.print(currentDrawingId);
  file.print("\"><rect width=\"800\" height=\"480\" fill=\"");
  char hex[8];
  colorToHex(board == BOARD_CHALK ? 0x13A4 : 0xFFFF, hex, sizeof(hex));
  file.print(hex);
  file.print("\"/>");
  if (board == BOARD_GRAPH) {
    file.print("<g stroke=\"#D0D0D0\" stroke-width=\"1\">");
    for (int x = 0; x < 800; x += 20) {
      file.printf("<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>",
                  x, DRAW_HEADER_H, x, DRAW_CANVAS_BOTTOM - 1);
    }
    for (int y = DRAW_HEADER_H; y < DRAW_CANVAS_BOTTOM; y += 20) {
      file.printf("<line x1=\"0\" y1=\"%d\" x2=\"799\" y2=\"%d\"/>", y, y);
    }
    file.print("</g>");
  }
  for (int i = 0; i < drawingSegmentCount; i++) {
    const DrawingSegment &segment = drawingSegments[i];
    colorToHex(segment.color, hex, sizeof(hex));
    file.printf("<line x1=\"%u\" y1=\"%u\" x2=\"%u\" y2=\"%u\" stroke=\"%s\" stroke-width=\"%u\" stroke-linecap=\"round\"/>",
                segment.x1, segment.y1, segment.x2, segment.y2, hex, segment.width);
  }
  file.print("</svg>");
  file.flush();
  bool ok = file.getWriteError() == 0;
  file.close();
  return ok;
}

static bool prepareDrawingSync(bool startNewBoard) {
  if (drawingSyncPending || drawingSyncWaitingForAck) {
    setDrawStatus("A drawing is already being sent");
    return false;
  }
  if (!saveDrawingFile()) return false;
  if (!writeDrawingSvg(selectedBoard, "/drawing-export.svg")) {
    SPIFFS.remove("/drawing-export.svg");
    warnDrawingStorage("Storage issue. Old board will be removed in 2:00");
    return false;
  }
  drawingSyncFile = SPIFFS.open("/drawing-export.svg", FILE_READ);
  if (!drawingSyncFile) {
    SPIFFS.remove("/drawing-export.svg");
    warnDrawingStorage("Storage issue. Old board will be removed in 2:00");
    return false;
  }
  drawingSyncId = currentDrawingId;
  drawingSyncBytesSent = 0;
  drawingSyncChunkIndex = 0;
  drawingSyncState = 0;
  drawingSyncStartedMs = millis();
  newBoardAfterSync = startNewBoard;
  drawingSyncPending = true;
  setDrawStatus("Waiting to send drawing to teacher");
  return true;
}

static bool sendDrawingPacket(const uint8_t *data, size_t size) {
  return noticeRemote != nullptr &&
         noticeRemote->writeValue((uint8_t *)data, size, true);
}

static void sendDrawingStep() {
  if (noticeRemote == nullptr || drawingSyncWaitingForAck) return;
  if (drawingSyncState == 0) {
    ClearDrawingStart packet = {};
    packet.version = CLEAR_LINK_VERSION;
    packet.kind = CLEAR_KIND_DRAWING_START;
    packet.drawingId = drawingSyncId;
    packet.board = selectedBoard;
    packet.totalBytes = drawingSyncFile.size();
    copyField(packet.student, sizeof(packet.student), studentName);
    if (!sendDrawingPacket((const uint8_t *)&packet, sizeof(packet))) {
      drawingSyncPending = false;
      drawingSyncFile.close();
      setDrawStatus("Could not start transfer; tap Save to retry");
      return;
    }
    drawingSyncState = 1;
    setDrawStatus("Sending drawing to teacher");
    return;
  }
  if (drawingSyncState == 1 && drawingSyncFile.available()) {
    ClearDrawingChunk packet = {};
    packet.version = CLEAR_LINK_VERSION;
    packet.kind = CLEAR_KIND_DRAWING_CHUNK;
    packet.drawingId = drawingSyncId;
    packet.chunkIndex = drawingSyncChunkIndex;
    size_t count = drawingSyncFile.read(packet.payload, CLEAR_DRAWING_CHUNK_SIZE);
    if (count == 0) {
      drawingSyncPending = false;
      drawingSyncFile.close();
      setDrawStatus("Drawing transfer stopped; tap Save to retry");
      return;
    }
    packet.payloadSize = count;
    size_t packetSize = offsetof(ClearDrawingChunk, payload) + count;
    if (!sendDrawingPacket((const uint8_t *)&packet, packetSize)) {
      drawingSyncPending = false;
      drawingSyncFile.close();
      setDrawStatus("Drawing transfer failed; tap Save to retry");
      return;
    }
    drawingSyncBytesSent += count;
    drawingSyncChunkIndex++;
    return;
  }
  if (drawingSyncState == 1) {
    if (drawingSyncBytesSent != drawingSyncFile.size()) {
      drawingSyncPending = false;
      drawingSyncFile.close();
      setDrawStatus("Drawing export was incomplete; tap Save to retry");
      return;
    }
    ClearDrawingEnd packet = {};
    packet.version = CLEAR_LINK_VERSION;
    packet.kind = CLEAR_KIND_DRAWING_END;
    packet.drawingId = drawingSyncId;
    if (!sendDrawingPacket((const uint8_t *)&packet, sizeof(packet))) {
      drawingSyncPending = false;
      drawingSyncFile.close();
      setDrawStatus("Drawing transfer failed; tap Save to retry");
      return;
    }
    drawingSyncPending = false;
    drawingSyncWaitingForAck = true;
    drawingSyncStartedMs = millis();
    setDrawStatus("Waiting for teacher card confirmation");
  }
}

static void finishDrawingSync(bool saved) {
  drawingSyncWaitingForAck = false;
  drawingSyncPending = false;
  drawingSyncState = 0;
  if (drawingSyncFile) drawingSyncFile.close();
  if (!saved) {
    newBoardAfterSync = false;
    setDrawStatus("Teacher card could not save; tap Save to retry");
    return;
  }
  setDrawStatus("Drawing saved to teacher card");
  if (newBoardAfterSync) {
    newBoardAfterSync = false;
    drawingSegmentCount = 0;
    currentDrawingId = newDrawingId();
    cursorVisible = false;
    saveDrawingFile();
    paintDrawingCanvas();
    setDrawStatus("New board ready");
  }
}

static void finishDrawingStroke() {
  if (!drawingStroke) return;
  drawingStroke = false;
  lastDrawX = -1;
  lastDrawY = -1;
  if (cursorVisible) {
    restoreCursorArea(cursorX, cursorY);
    cursorVisible = false;
  }
  saveDrawingFile();
}

static void updateDrawingStorageWarning() {
  static unsigned long lastShownSeconds = ULONG_MAX;
  if (!storageWarningActive) {
    lastShownSeconds = ULONG_MAX;
    if (drawStatusUntilMs != 0 &&
        (long)(millis() - drawStatusUntilMs) >= 0) {
      setDrawStatus("Draw on the board");
    }
    return;
  }
  unsigned long elapsed = millis() - storageWarningStartedMs;
  if (elapsed < DRAW_WARNING_MS) {
    unsigned long remaining = (DRAW_WARNING_MS - elapsed + 999) / 1000;
    if (remaining == lastShownSeconds) return;
    lastShownSeconds = remaining;
    char message[64];
    snprintf(message, sizeof(message), "Storage full: old board removed in %lu:%02lu",
             remaining / 60, remaining % 60);
    setDrawStatus(message);
    return;
  }

  storageWarningActive = false;
  if (!deleteOldestLocalDrawing()) {
    storageSavePending = false;
    setDrawStatus("Storage full; no old local board was available to remove");
    return;
  }
  if (storageSavePending && !saveDrawingFile()) {
    storageSavePending = false;
    storageWarningActive = false;
    setDrawStatus("Storage full; this drawing could not be saved");
  }
}

// =============================================================================
// =============================================================================
//
//   END OF SCREEN DRAWING
//
// =============================================================================
// =============================================================================

// Capitalize the first letter, and the letter after a space, hyphen,
// or apostrophe, so "mary ann" becomes "Mary Ann" and "o'brien" becomes "O'Brien".
static bool nameNeedsCapital() {
  char *text = activeText();
  int n = strlen(text);
  if (n == 0) {
    return true;
  }
  char prev = text[n - 1];
  return prev == ' ' || prev == '-' || prev == '\'';
}

// Change nameEdit for one key, then redraw. A key that cannot be
// used (name already full, or a second space in a row) returns
// without redrawing.
static void applyKey(const KeyHit *key) {
  char *text = activeText();
  int limit = typeLimit();
  int n = strlen(text);
  if (key->action == KEY_BACKSPACE) {
    if (n > 0) {
      text[n - 1] = 0;
    }
  } else if (key->action == KEY_CLEAR) {
    text[0] = 0;
  } else if (key->action == KEY_CHAR || key->action == KEY_SPACE) {
    char c = (key->action == KEY_SPACE) ? ' ' : key->ch;
    if (n >= limit) {
      return;
    }
    if ((c == ' ' || c == '-' || c == '\'') && (n == 0 || text[n - 1] == c)) {
      return;
    }
    // Keys are labeled in capitals. Subtracting 'A' and adding 'a'
    // turns that capital into the lowercase letter, unless this
    // spot is the start of a word. A message stays as typed.
    if (c >= 'A' && c <= 'Z' && !nameNeedsCapital()) {
      c = c - 'A' + 'a';
    }
    text[n] = c;
    text[n + 1] = 0;
  } else {
    return;
  }
  paintNameEditor();
}

// Remove spaces at either end so a name made of only spaces is empty,
// and Save can refuse it.
static void trimName(char *text) {
  int n = strlen(text);
  while (n > 0 && text[n - 1] == ' ') {
    text[--n] = 0;
  }
  int start = 0;
  while (text[start] == ' ') {
    start++;
  }
  if (start > 0) {
    int i = 0;
    while (text[start + i]) {
      text[i] = text[start + i];
      i++;
    }
    text[i] = 0;
  }
}

// Remember which screen is open, then draw it. The remember step
// has to happen first so a tap during the draw is handled correctly
// on the next loop.
static void showMainMenu() {
  crumbTop = 0;
  viewItems = mainItems;
  viewCount = MAIN_COUNT;
  viewTitle = "What do you need?";
  openItem = nullptr;
  breathRunning = false;
  currentScreen = SCREEN_MENU;
  paintList();
}

// Tell the teacher about one real choice. Opening a list does not
// call this. K may see a short word, but the note uses the full one.
static void notifyItem(const Item *item, const char *choiceText) {
  ClearNote note = {};
  note.version = CLEAR_LINK_VERSION;
  note.kind = CLEAR_KIND_NOTICE;
  note.asksTeacher = itemAsks(item) ? 1 : 0;
  note.answer = CLEAR_ANSWER_NONE;
  note.eventId = nextEventId++;
  waitingId = note.eventId;
  teacherReply = CLEAR_ANSWER_NONE;
  noticeSent = false;
  note.timeSent = (uint32_t)time(nullptr);
  copyField(note.student, sizeof(note.student), studentName);
  copyField(note.feeling, sizeof(note.feeling), item->topic);
  copyField(note.choice, sizeof(note.choice), choiceText);
  outboundNote = note;
  needSend = true;
  nextBleMs = 0;
  saveClock();
}

static void showDetail(const Item *item) {
  openItem = item;
  breathRunning = false;
  currentScreen = SCREEN_SELECTED;
  paintSelected();
}

static void openChildList(const Item *item) {
  if (crumbTop < 5) {
    crumbs[crumbTop].items = viewItems;
    crumbs[crumbTop].count = viewCount;
    crumbs[crumbTop].title = viewTitle;
    crumbTop++;
  }
  viewItems = item->kids;
  viewCount = item->kidCount;
  viewTitle = buttonLabel(item);
  currentScreen = SCREEN_MENU;
  paintList();
}

static void openMessage(const Item *item) {
  openItem = item;
  typeMode = TYPE_MESSAGE;
  messageEdit[0] = 0;
  currentScreen = SCREEN_NAME;
  paintNameEditor();
}

static void openWrite() {
  for (int i = 0; i < 3; i++) {
    writeLines[i][0] = 0;
  }
  currentScreen = SCREEN_WRITE;
  paintWrite();
}

static void openDrawingMenu() {
  drawingStroke = false;
  lastDrawX = -1;
  lastDrawY = -1;
  currentScreen = SCREEN_DRAW_MENU;
  paintDrawingMenu();
}

static void openDrawingCanvas(int board) {
  drawingStroke = false;
  lastDrawX = -1;
  lastDrawY = -1;
  cursorVisible = false;
  bool loaded = loadDrawingFile(board);
  currentScreen = SCREEN_DRAW_CANVAS;
  if (loaded) {
    setDrawStatus(drawStorageReady ? "Draw here; strokes save on this panel" :
                                     "Local storage unavailable; drawing will not persist");
  }
  paintDrawingCanvas();
}

static void resetDrawingBoard() {
  drawingStroke = false;
  lastDrawX = -1;
  lastDrawY = -1;
  cursorVisible = false;
  drawingSegmentCount = 0;
  currentDrawingId = newDrawingId();
  storageSavePending = false;
  storageWarningActive = false;
  if (drawStorageReady && SPIFFS.exists(drawingFilePath(selectedBoard)) &&
      !SPIFFS.remove(drawingFilePath(selectedBoard))) {
    setDrawStatus("Could not remove the saved board");
    return;
  }
  paintDrawingCanvas();
  setDrawStatus("Board reset. Previous teacher copy is unchanged");
}

// The row the student pressed. A list opens the next page and does
// not notify. Every other action notifies once.
static void chooseItem(int index) {
  if (index < 0 || index >= viewCount) {
    return;
  }
  const Item *item = &viewItems[index];
  if (item->action == ACT_DRAWING) {
    openDrawingMenu();
    return;
  }
  if (item->action == ACT_OPEN) {
    openChildList(item);
    return;
  }
  if (item->action == ACT_TYPE) {
    openMessage(item);
    return;
  }
  if (item->action == ACT_WRITE) {
    openItem = item;
    openWrite();
    return;
  }
  notifyItem(item, item->label);
  showDetail(item);
}

// Start the 4-second count. The notice was already sent when
// Box breathing was opened.
static void beginBreath() {
  breathRunning = true;
  breathMs = millis();
  breathDrawMs = millis();
  paintSelected();
}

// Detail page and the writing page go back to the list.
// A nested list goes back to the list that opened it.
static void goBack() {
  breathRunning = false;
  if (currentScreen == SCREEN_SELECTED || currentScreen == SCREEN_WRITE) {
    currentScreen = SCREEN_MENU;
    paintList();
    return;
  }
  if (crumbTop > 0) {
    crumbTop--;
    viewItems = crumbs[crumbTop].items;
    viewCount = crumbs[crumbTop].count;
    viewTitle = crumbs[crumbTop].title;
    currentScreen = SCREEN_MENU;
    paintList();
  }
}

// Leave settings. If the name changed, or settings was opened from
// the greeting, show the greeting again so the new name can be seen.
// screenMs is reset so that greeting gets a fresh two seconds.
static void showReturnScreen() {
  int dest = returnScreen;
  if (nameDirty || dest == SCREEN_HELLO || dest == SCREEN_SETTINGS || dest == SCREEN_NAME) {
    nameDirty = false;
    screenMs = millis();
    currentScreen = SCREEN_HELLO;
    paintHello();
    return;
  }
  currentScreen = dest;
  if (dest == SCREEN_SELECTED) {
    paintSelected();
  } else if (dest == SCREEN_WRITE) {
    paintWrite();
  } else if (dest == SCREEN_MENU || dest == SCREEN_SUBMENU) {
    currentScreen = SCREEN_MENU;
    paintList();
  } else {
    showMainMenu();
  }
}

// Remember the screen we came from, then draw settings.
static void openSettings() {
  returnScreen = currentScreen;
  currentScreen = SCREEN_SETTINGS;
  paintSettings();
}

// Start the keyboard from the name that is already saved.
// Typing changes nameEdit only, until Save.
static void openNameEditor() {
  typeMode = TYPE_NAME;
  copyName(nameEdit, studentName);
  currentScreen = SCREEN_NAME;
  paintNameEditor();
}

// Drop the typed name and go back to settings.
static void closeNameEditor() {
  if (typeMode == TYPE_WRITE) {
    currentScreen = SCREEN_WRITE;
    paintWrite();
    return;
  }
  if (typeMode == TYPE_MESSAGE) {
    currentScreen = SCREEN_MENU;
    paintList();
    return;
  }
  currentScreen = SCREEN_SETTINGS;
  paintSettings();
}

// Put the typed message in front of its lead, then tell the teacher.
// An empty message stays on the keyboard.
static void sendTypedMessage() {
  trimName(messageEdit);
  if (messageEdit[0] == 0 || openItem == nullptr) {
    paintNameEditor();
    return;
  }
  char sent[MESSAGE_MAX + 1];
  if (openItem->lead != nullptr) {
    snprintf(sent, sizeof(sent), "%s%s", openItem->lead, messageEdit);
  } else {
    snprintf(sent, sizeof(sent), "%s", messageEdit);
  }
  notifyItem(openItem, sent);
  showDetail(openItem);
}

static void saveWriteLine() {
  trimName(messageEdit);
  copyField(writeLines[writeSlot], WRITE_MAX + 1, messageEdit);
  currentScreen = SCREEN_WRITE;
  paintWrite();
}

static void finishWriting() {
  if (openItem != nullptr) {
    notifyItem(openItem, "Wrote how I feel");
    showDetail(openItem);
  }
}

// Keep the typed name, write it to flash, and return to settings.
// An empty name is refused and the keyboard stays open.
static void saveNameEdit() {
  trimName(nameEdit);
  if (nameEdit[0] == 0) {
    paintNameEditor();
    return;
  }
  copyName(studentName, nameEdit);
  nameDirty = true;
  saveSettings();
  closeNameEditor();
}

// Snap to a step of 10, then update the light, the flash, and the
// settings drawing. Do nothing if the step did not actually change,
// so a repeated tap on the same spot does not redraw.
static void setBrightnessPct(int pct) {
  if (pct < BRIGHT_MIN) {
    pct = BRIGHT_MIN;
  }
  if (pct > BRIGHT_MAX) {
    pct = BRIGHT_MAX;
  }
  pct = (pct / 10) * 10;
  if (pct == brightnessPct) {
    return;
  }
  brightnessPct = pct;
  applyBrightness();
  saveSettings();
  paintSettings();
}

// A tap on the bar. The left end is 20% and the right end is 100%.
// The last line rounds to the nearest 10 so the bar matches the + and - buttons.
static int brightnessFromX(int x) {
  int rel = x - BAR_X;
  if (rel < 0) {
    rel = 0;
  }
  if (rel > BAR_W) {
    rel = BAR_W;
  }
  int pct = BRIGHT_MIN + rel * (BRIGHT_MAX - BRIGHT_MIN) / BAR_W;
  return ((pct + 5) / 10) * 10;
}

// Save the age group and redraw settings. The lists use the new
// type size the next time they are drawn.
static void setGrade(int index) {
  if (index < 0 || index >= GRADE_COUNT || index == gradeLevel) {
    return;
  }
  gradeLevel = index;
  saveSettings();
  paintSettings();
}

// Ask the GT911 touch chip whether a finger is down, and where.
// The chip holds one report until we read it and write 0 back.
// If that report is thrown away, the sketch keeps the old finger
// and then ignores every new tap. A quiet gap means the finger is up.
static bool readFinger(int32_t *x, int32_t *y) {
  Wire.beginTransmission(touchAddr);
  Wire.write(0x81);
  Wire.write(0x4E);
  if (Wire.endTransmission(false) != 0) {
    fingerDown = false;
    return false;
  }
  if (Wire.requestFrom(touchAddr, (uint8_t)9) < 1) {
    fingerDown = false;
    return false;
  }
  uint8_t buf[9] = {};
  int got = 0;
  while (Wire.available() && got < 9) {
    buf[got] = Wire.read();
    got++;
  }
  // Bit 7 means a new report is waiting. The low bits are how many fingers.
  if (buf[0] & 0x80) {
    uint8_t points = buf[0] & 0x0F;
    if (points > 0 && got >= 6) {
      int32_t rx = buf[2] | ((int32_t)buf[3] << 8);
      int32_t ry = buf[4] | ((int32_t)buf[5] << 8);
      if (rx >= 0 && rx <= 799 && ry >= 0 && ry <= 479) {
        fingerX = rx;
        fingerY = ry;
        fingerDown = true;
        fingerMs = millis();
      }
    } else {
      fingerDown = false;
    }
    Wire.beginTransmission(touchAddr);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0x00);
    Wire.endTransmission();
  } else if (fingerDown && (millis() - fingerMs) > 80) {
    fingerDown = false;
  }
  if (!fingerDown) {
    return false;
  }
  *x = fingerX;
  *y = fingerY;
  return true;
}

// Runs once. Order matters: the expander and the saved settings are
// ready before the panel starts, and the backlight level is applied
// after init() because init() is what attaches PWM to pin 2.
void setup() {
  // Light the backlight immediately so the panel is not black during
  // the rest of startup. applyBrightness() replaces this with PWM.
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  initExpander();
  loadSettings();          // name, brightness, and grade from the last run
  lcd.init();              // start the panel with the wiring stored above
  lcd.setRotation(0);      // 0 matches the touch map. Another value would move taps off the buttons.
  // The panel start also uses these two pins. Take them back for the
  // touch chip, at the slower speed that this glass answers on.
  Wire.begin(19, 20);
  Wire.setClock(100000);
  Wire.beginTransmission(0x5D);
  if (Wire.endTransmission() != 0) {
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) {
      touchAddr = 0x14;
    }
  }
  applyBrightness();
  drawStorageReady = SPIFFS.begin(false);
  currentDrawingId = newDrawingId();

  startTeacherLink();     // radio to the teacher device, not a phone hotspot
  screenMs = millis();    // start the two-second greeting timer
  paintHello();
}

// Repeats forever. A finger is read on every pass. The teacher search
// runs in the background so it does not hold the touch chip.
void loop() {
  if (millis() - clockMs >= 60000) {
    clockMs = millis();
    saveClock();
  }

  // A Yes or No can arrive while a finger is down. Apply it here, then
  // refresh the footer. Settings and the keyboard have no footer.
  if (replyReady) {
    ClearNote note;
    noInterrupts();
    note = pendingReply;
    replyReady = false;
    interrupts();
    if (note.version == CLEAR_LINK_VERSION &&
        note.kind == CLEAR_KIND_REPLY &&
        note.eventId == waitingId &&
        strcmp(note.student, studentName) == 0) {
      teacherReply = note.answer;
      if (currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_NAME &&
          currentScreen != SCREEN_DRAW_MENU && currentScreen != SCREEN_DRAW_CANVAS) {
        paintStatus();
      }
    }
  }

  if (drawingResultReady) {
    ClearDrawingResult result;
    noInterrupts();
    result = pendingDrawingResult;
    drawingResultReady = false;
    interrupts();
    if (drawingSyncWaitingForAck && result.drawingId == drawingSyncId) {
      finishDrawingSync(result.saved != 0);
    }
  }
  if (drawingSyncWaitingForAck &&
      (millis() - drawingSyncStartedMs) >= 15000) {
    finishDrawingSync(false);
  }
  if (storageWarningActive || currentScreen == SCREEN_DRAW_CANVAS) {
    updateDrawingStorageWarning();
  }

  // The breathing square and the count move while the student watches.
  // A full redraw would flash the Back button, so only the middle changes.
  if (currentScreen == SCREEN_SELECTED && breathRunning && (millis() - breathDrawMs) >= 100) {
    breathDrawMs = millis();
    paintBreathMotion();
  }

  // Greeting timer. Opening settings leaves SCREEN_HELLO, so the timer
  // does not fire while the teacher is still in settings.
  if (currentScreen == SCREEN_HELLO && (millis() - screenMs) >= HELLO_MS) {
    showMainMenu();
  }

  // readFinger fills tx and ty while the finger is down. Touch-down starts
  // drawing; touchWasDown prevents a held finger from starting new strokes.
  int32_t tx = 0;
  int32_t ty = 0;
  bool touchDown = readFinger(&tx, &ty);
  if (currentScreen == SCREEN_DRAW_CANVAS) {
    if (!touchDown) {
      finishDrawingStroke();
    } else if (!drawingStroke && !touchWasDown &&
               ty >= DRAW_HEADER_H && ty < DRAW_CANVAS_BOTTOM) {
      int drawY = ty;
      int minDrawY = DRAW_HEADER_H + drawingInkWidth() / 2;
      if (drawY < minDrawY) {
        drawY = minDrawY;
      }
      drawingStroke = true;
      lastDrawX = tx;
      lastDrawY = drawY;
      drawInkSegment(tx, drawY, tx, drawY);
      moveDrawingCursor(tx, drawY);
    } else if (drawingStroke && ty >= DRAW_HEADER_H &&
               ty < DRAW_CANVAS_BOTTOM) {
      drawInkSegment(lastDrawX, lastDrawY, tx, ty);
      moveDrawingCursor(tx, ty);
      lastDrawX = tx;
      lastDrawY = ty;
    } else if (drawingStroke) {
      finishDrawingStroke();
    }
  }
  if (touchDown && !touchWasDown && (millis() - tapMs) >= TAP_GAP_MS) {
    tapMs = millis();
    bool onGearScreen = currentScreen == SCREEN_HELLO ||
                        currentScreen == SCREEN_MENU ||
                        currentScreen == SCREEN_SELECTED;
    // The gear is checked before the rest of the page. On the greeting,
    // any other tap skips ahead to the first list.
    if (onGearScreen && inBox(tx, ty, GEAR_X, GEAR_Y, GEAR_W, GEAR_H)) {
      openSettings();
    } else if (currentScreen == SCREEN_HELLO) {
      showMainMenu();
    // A choice list: Back, or the row under the finger.
    } else if (currentScreen == SCREEN_MENU) {
      if (crumbTop > 0 && inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        goBack();
      } else {
        int height = rowHeightFor(viewCount);
        for (int i = 0; i < viewCount; i++) {
          if (inBox(tx, ty, ROW_X, rowTopFor(i, viewCount), ROW_W, height)) {
            chooseItem(i);
            break;
          }
        }
      }
    } else if (currentScreen == SCREEN_DRAW_MENU) {
      if (inBox(tx, ty, DRAW_BACK_X, DRAW_BACK_Y, DRAW_BACK_W, DRAW_BACK_H)) {
        showMainMenu();
      } else if (inBox(tx, ty, 24, 82, 752, 96)) {
        openDrawingCanvas(BOARD_GRAPH);
      } else if (inBox(tx, ty, 24, 190, 752, 96)) {
        openDrawingCanvas(BOARD_WHITE);
      } else if (inBox(tx, ty, 24, 298, 752, 96)) {
        openDrawingCanvas(BOARD_CHALK);
      }
    } else if (currentScreen == SCREEN_DRAW_CANVAS) {
      if (inBox(tx, ty, DRAW_BACK_X, DRAW_BACK_Y, DRAW_BACK_W, DRAW_BACK_H)) {
        openDrawingMenu();
      } else if (selectedBoard == BOARD_CHALK &&
                 inBox(tx, ty, DRAW_COLOR_X[0], DRAW_COLOR_Y,
                       DRAW_COLOR_X[DRAW_COLOR_COUNT - 1] +
                         DRAW_COLOR_W - DRAW_COLOR_X[0], DRAW_COLOR_H)) {
        for (int i = 0; i < DRAW_COLOR_COUNT; i++) {
          if (inBox(tx, ty, DRAW_COLOR_X[i], DRAW_COLOR_Y,
                    DRAW_COLOR_W, DRAW_COLOR_H)) {
            selectedChalkColor = i;
            saveDrawingFile();
            paintDrawingCanvas();
            break;
          }
        }
      } else if (inBox(tx, ty, DRAW_SAVE_X, DRAW_BUTTON_Y, 80, DRAW_BUTTON_H)) {
        prepareDrawingSync(false);
      } else if (inBox(tx, ty, DRAW_RESET_X, DRAW_BUTTON_Y, 104, DRAW_BUTTON_H)) {
        resetDrawingBoard();
      } else if (inBox(tx, ty, DRAW_NEW_X, DRAW_BUTTON_Y, 96, DRAW_BUTTON_H)) {
        prepareDrawingSync(true);
      }
      // Detail page: Back, or Begin on the breathing screen.
    } else if (currentScreen == SCREEN_SELECTED) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        goBack();
      } else if (!breathRunning && openItem != nullptr && openItem->action == ACT_BREATHE &&
                 inBox(tx, ty, 24, 336, 752, 60)) {
        beginBreath();
      }
    } else if (currentScreen == SCREEN_WRITE) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        goBack();
      } else if (inBox(tx, ty, DONE_X, DONE_Y, DONE_W, DONE_H)) {
        finishWriting();
      } else {
        for (int i = 0; i < 3; i++) {
          int top = 80 + i * 78;
          if (inBox(tx, ty, ROW_X, top, ROW_W, 68)) {
            writeSlot = i;
            typeMode = TYPE_WRITE;
            copyField(messageEdit, sizeof(messageEdit), writeLines[i]);
            currentScreen = SCREEN_NAME;
            paintNameEditor();
            break;
          }
        }
      }
    // Settings: Done, the name row, brightness, or a grade row.
    // The checks run in that order so a tap cannot hit two controls.
    } else if (currentScreen == SCREEN_SETTINGS) {
      if (inBox(tx, ty, DONE_X, DONE_Y, DONE_W, DONE_H)) {
        showReturnScreen();
      } else if (inBox(tx, ty, ROW_X, NAME_Y, ROW_W, NAME_H)) {
        openNameEditor();
      } else if (inBox(tx, ty, MINUS_X, BRIGHT_Y, STEP_W, BRIGHT_H)) {
        setBrightnessPct(brightnessPct - 10);
      } else if (inBox(tx, ty, PLUS_X, BRIGHT_Y, STEP_W, BRIGHT_H)) {
        setBrightnessPct(brightnessPct + 10);
      } else if (inBox(tx, ty, BAR_X, BRIGHT_Y, BAR_W, BRIGHT_H)) {
        setBrightnessPct(brightnessFromX(tx));
      } else {
        for (int i = 0; i < GRADE_COUNT; i++) {
          if (inBox(tx, ty, ROW_X, gradeRowTop(i), ROW_W, GRADE_H)) {
            setGrade(i);
            break;
          }
        }
      }
    // Keyboard: Save, Back, or whichever key contains the tap.
    } else if (currentScreen == SCREEN_NAME) {
      if (inBox(tx, ty, DONE_X, DONE_Y, DONE_W, DONE_H)) {
        if (typeMode == TYPE_MESSAGE) {
          sendTypedMessage();
        } else if (typeMode == TYPE_WRITE) {
          saveWriteLine();
        } else {
          saveNameEdit();
        }
      } else if (inBox(tx, ty, NAME_BACK_X, NAME_BACK_Y, NAME_BACK_W, NAME_BACK_H)) {
        closeNameEditor();
      } else {
        KeyHit keys[KEY_MAX];
        int count = collectKeys(keys);
        for (int i = 0; i < count; i++) {
          if (inBox(tx, ty, keys[i].x, keys[i].y, keys[i].w, keys[i].h)) {
            applyKey(&keys[i]);
            break;
          }
        }
      }
    }
  }
  // Remember this pass so the next one can tell a new press from a held finger.
  // The teacher search runs only after the finger is up, so a tap is
  // not thrown away while the radio is busy.
  touchWasDown = touchDown;
  if (!touchDown) {
    blePump();
  }
}
