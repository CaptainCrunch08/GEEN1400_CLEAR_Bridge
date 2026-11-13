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
  For the website's USB settings link, set USB CDC On Boot to Enabled.
  Bluetooth settings work without that switch.

  A student selection is sent by Bluetooth to the teacher device. There
  is no phone app. The teacher answers Yes or No, and that answer shows
  along the bottom of this panel. The time sent with each selection
  comes from this panel's clock.

  Green buttons are something the student can try.
  Orange buttons ask the teacher. Both are sent, so the teacher knows
  what the student chose. Opening a list does not send. A typed message
  sends when the student presses Send. Breathing sends when it is
  opened, and the count starts when the student presses Begin.
  Grades K through 2 blow out a candle. Older grades use the box.

  The panel stays covered until the class PIN is entered. That is the
  same four digits chosen on the teacher device. Ten seconds with no
  touch covers the screen again, so a message is not left in view.
  The name saved on the panel is locked with that PIN.

  The gear in the top right opens settings. A teacher can change the
  name on the startup screen, the brightness, and the type size.
  The same settings can be changed from the CLEAR website. Plug the
  panel in by USB-C, or connect by Bluetooth. The website asks for the
  settings password. That password is not the class PIN.
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
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "LogoMark.h"
#include "ToolboxMark.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <SPIFFS.h>
#include <Wire.h>          // I2C bus, used by the touch chip and the small expander
#include <Preferences.h>   // saves a few settings in flash across restarts
#include <esp_system.h>
#include <LovyanGFX.hpp>   // draws text, rectangles, and reads the touch panel
#include "driver/i2s.h"    // onboard speaker: I2S data 17, word select 18, clock 42
#include <math.h>
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This sketch requires an ESP32-S3 board. In Arduino IDE select ESP32S3 Dev Module."
#endif
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include "CLEAR_Link.h"
#include "CLEAR_Seal.h"    // radio message shared with the teacher sketch

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
// The greeting stays up this long, then the thermometer appears on its own.
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
static const int SCREEN_LOCK = 9;       // class PIN, or the privacy cover
static const int SCREEN_THERMO = 10;    // the energy thermometer
static const int SCREEN_ZONE = 11;      // Feel Better, Ask for Help, Draw or write
static const int SCREEN_TOOLS = 12;     // teacher turns tools on for this student
static const int SCREEN_GROUND = 13;    // see, hear, feel
static const int SCREEN_FIRST = 14;     // first, then
static const int SCREEN_SCHED = 15;     // the visual schedule
static const int SCREEN_SOOTHE = 16;    // coping tools
static const int SCREEN_ASK = 17;       // approved accommodations
static const int SCREEN_EXPRESS = 18;   // chalkboard or notebook
static const int SCREEN_FEED = 19;      // did that help?
static const int SCREEN_RACE = 20;      // five quiet minutes with Bridger
static const int SCREEN_PLAN = 21;      // race, schedule, and first then
static const int LOCK_ENTER = 0;
static const int LOCK_CREATE = 1;
static const int LOCK_CHANGE = 2;
static const int LOCK_SET_PASS = 3;       // teacher chooses the settings password
static const int LOCK_SET_PASS_AGAIN = 4; // the same password a second time
static const int LOCK_SETTINGS = 5;       // password required before settings
static const unsigned long PRIVACY_IDLE_MS = 10000;

static const int DRAW_HEADER_H = 108;
static const int DRAW_CANVAS_BOTTOM = 420;
static const int DRAW_BACK_X = 8;
static const int DRAW_BACK_Y = 8;
static const int DRAW_BACK_W = 82;
static const int DRAW_BACK_H = 44;
static const int DRAW_SAVE_X = 98;
static const int DRAW_RESET_X = 192;
static const int DRAW_NEW_X = 310;
static const int DRAW_READ_X = 410;
static const int DRAW_BUTTON_Y = 8;
static const int DRAW_BUTTON_H = 44;
static const int DRAW_COLOR_Y = 58;
static const int DRAW_COLOR_W = 90;
static const int DRAW_COLOR_H = 36;
static const int DRAW_COLOR_X[] = {12, 110, 208, 306, 404, 502, 600, 698};
static const int DRAW_COLOR_COUNT = 8;
static const uint16_t DRAW_CHALK_COLORS[DRAW_COLOR_COUNT] = {
  0xFD20, 0xFFFF, 0x001F, 0x07E0, 0xFFE0, 0xFB56, 0xF800, 0x9816
};
static const char *DRAW_CHALK_NAMES[DRAW_COLOR_COUNT] = {
  "Orange", "White", "Blue", "Green", "Yellow", "Pink", "Red", "Purple"
};
static const uint16_t DRAW_CHALK_INK[DRAW_COLOR_COUNT] = {
  0x1082, 0x1082, 0xFFFF, 0x1082, 0x1082, 0x1082, 0xFFFF, 0xFFFF
};
static const uint32_t DRAW_FILE_MAGIC = 0x434C4452;
static const uint32_t DRAW_OCR_FILE_MAGIC = 0x434C4F43;
static const uint8_t DRAW_FILE_VERSION = 1;
static const int DRAW_SEGMENT_MAX = 2400;
static const size_t DRAW_OCR_MAX_BYTES = 600;
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

struct DrawingOcrFileHeader {
  uint32_t magic;
  uint32_t drawingId;
  uint16_t textBytes;
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
static const int TYPE_TOOL = 3;
static const int TYPE_TEACHER = 4;
static const int NAME_Y = 58;
static const int NAME_H = 40;
static const int TEACH_Y = 102;
static const int TEACH_H = 40;
static const int CHANGE_W = 128;
static const int CHANGE_H = 40;
static const int CHANGE_X = ROW_X + ROW_W - 12 - CHANGE_W;
static const int CHANGE_Y = NAME_Y + 8;

static const int BRIGHT_LABEL_Y = 146;
static const int BRIGHT_Y = 162;
static const int BRIGHT_H = 28;
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

static const int GRADE_NOTE_Y = 192;
static const int GRADE_TOP = 210;
static const int GRADE_H = 34;
static const int GRADE_GAP = 2;
static const int LOOK_Y = 358;
static const int LOOK_H = 36;
// Bridger's on/off row sits under the age groups.
static const int BEAVER_Y = 400;
static const int BEAVER_H = 46;
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
static const uint16_t COL_BG = 0x0000;
static const uint16_t COL_FG = 0xFFFF;
static const uint16_t COL_ROW = 0x2104;
static uint16_t COL_TRY = 0x07E0;
static uint16_t COL_ASK = 0xFD20;
static const uint16_t COL_FOOT = 0x2104;
static uint16_t COL_MINT = 0xE79D;
static uint16_t COL_CREAM = 0xFFBD;
static uint16_t COL_LILAC = 0xEF3E;
static uint16_t COL_PALE = 0xE79F;
static const uint16_t COL_ICON = 0x2B49;
static const uint16_t COL_LOGO = 0x2B49;

// The thermometer on the first screen. Bottom is low energy. Top is high.
// The bulb and the lowest band are blue. The band under red is orange.
// Yellow, orange, and red tell the teacher. The name is what the teacher sees.
struct FeelingZone {
  const char *name;
  uint16_t color;
  bool tellTeacher;
};

static const FeelingZone feelingZones[] = {
  {"Blue Zone", 0x3B9F, false},
  {"Green Zone", 0x07E0, false},
  {"Yellow Zone", 0xFFE0, true},
  {"Orange Zone", 0xFBA0, true},
  {"Red Zone", 0xF800, true},
};
static const int ZONE_COUNT = 5;
static const int TUBE_X = 150;
static const int TUBE_W = 52;
static const int TUBE_TOP = 108;
static const int TUBE_BOTTOM = 300;
static const int BULB_R = 46;
static const int LABEL_X = 328;
static const int LABEL_W = 196;
static const int LABEL_H = 34;
static const int THIS_X = 500;
static const int THIS_Y = 360;
static const int THIS_W = 270;
static const int THIS_H = 52;

// Which tools this student's panel offers. The teacher sets these.
static const uint8_t TOOL_ZONES = 1;
static const uint8_t TOOL_GROUND = 2;
static const uint8_t TOOL_FIRST = 4;
static const uint8_t TOOL_SCHED = 8;
static const uint8_t TOOL_BREATH = 16;
static const uint8_t TOOL_ALL = 31;

// Accommodations a teacher can approve for this student.
// The ones that are on appear beside the coping tools.
// Movement break, Quiet corner, and Fidget start on.
static const char *ACCOMMODATIONS[] = {
  "Movement break",
  "Quiet corner",
  "Headphones",
  "Extra time",
  "Fidget",
  "Sit close",
  "Water break",
  "Chunk work",
  "Helper",
  "Stand up"
};
static const int ACC_COUNT = 10;
static const uint16_t ACC_DEFAULT = (1u << 0) | (1u << 1) | (1u << 4);

// See, hear, feel is a short 3-2-1. The student finds 3 things
// they can see, 2 they can touch, and 1 sound. Each see step
// names a look. Change a word in quotes to change the hunt.
static const char *SEE_LOOKS[] = {
  "green", "blue", "red", "yellow", "round",
  "pointy", "shiny", "soft", "big", "small"
};
static const int SEE_LOOK_COUNT = 10;

// School activities a K-5 class actually does. The teacher taps one
// to fill the green side. Custom, drawn after this list, types a word.
static const char *SCHOOL_ACTIVITIES[] = {
  "Reading", "Math", "Writing", "Science", "Recess",
  "Lunch", "Snack", "Art", "Music", "PE",
  "Centers", "Carpet", "Clean up", "Line up", "Choice",
  "Work", "Break", "Story", "Help"
};
static const int SCHOOL_ACTIVITY_COUNT = 19;
static const int ACT_COLS = 5;
static const int ACT_X = 34;
static const int ACT_Y = 230;
static const int ACT_W = 140;
static const int ACT_H = 50;
static const int ACT_STEP_X = 148;
static const int ACT_STEP_Y = 58;
static const int HALF_Y = 58;
static const int HALF_H = 148;
static const int HALF_W = 376;
static const int HALF_LEFT = 16;
static const int HALF_RIGHT = 408;
static const int ACT_BOX_X = 16;
static const int ACT_BOX_Y = 218;
static const int ACT_BOX_W = 768;
static const int ACT_BOX_H = 250;

// Four schedule blocks in a row, with an arrow in each gap.
static const int SCHED_COUNT = 4;
static const int SCHED_BLOCK_W = 148;
static const int SCHED_ARROW = 36;
static const int SCHED_X0 = 50;
static const int SCHED_SHOW_Y = 128;
static const int SCHED_SHOW_H = 128;
static const int SCHED_EDIT_Y = 64;
static const int SCHED_EDIT_H = 142;
static const int SCHED_NEXT_X = 24;
static const int SCHED_NEXT_Y = 292;
static const int SCHED_NEXT_W = 752;
static const int SCHED_NEXT_H = 72;

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
  {"Box breathing", "Distracted", BREATH_HELP, "Blow", ACT_BREATHE, nullptr, nullptr, 0},
  {"Quieter class please", "Distracted", ASK_HELP, "Quiet", ACT_ASK, nullptr, nullptr, 0},
  {"Ask for a fidget toy", "Distracted", ASK_HELP, "Fidget", ACT_ASK, nullptr, nullptr, 0},
  {"Custom message", "Distracted", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item worriedKids[] = {
  {"Talk to the teacher", "Worried", ASK_HELP, "Talk", ACT_ASK, nullptr, nullptr, 0},
  {"Box breathing", "Worried", BREATH_HELP, "Blow", ACT_BREATHE, nullptr, nullptr, 0},
  {"Positive self-talk", "Worried", "1. Notice the words in your head.\n2. Ask: is that fair?\n3. Say one kinder true sentence.\n\nI can try one small step.\nI can ask for help.", "Kind", ACT_TRY, nullptr, nullptr, 0},
  {"Put the worry outside", "Worried", "1. Name the worry.\n2. Say it like it is outside you.\n3. Name one thing you can do.", "Worry", ACT_TRY, nullptr, nullptr, 0},
  {"Custom message", "Worried", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item upsetKids[] = {
  {"Could I take a walk?", "Upset", ASK_HELP, "Walk", ACT_ASK, nullptr, nullptr, 0},
  {"Box breathing", "Upset", BREATH_HELP, "Blow", ACT_BREATHE, nullptr, nullptr, 0},
  {"Talk in private", "Upset", ASK_HELP, "Private", ACT_ASK, nullptr, nullptr, 0},
  {"Talk to somebody", "Upset", "Ask to talk with somebody,\nsuch as a counselor.", "Someone", ACT_ASK, nullptr, nullptr, 0},
  {"Use the COPE steps", "Upset", "C  Calm your body.\nO  Notice the thought. Change it.\nP  Plan one other thing to do.\nE  Ask: do I feel cooler or hotter?", "COPE", ACT_TRY, nullptr, nullptr, 0},
  {"Custom message", "Upset", TYPE_HELP, "Write", ACT_TYPE, nullptr, nullptr, 0}
};
static const Item frustratedKids[] = {
  {"Box breathing", "Frustrated", BREATH_HELP, "Blow", ACT_BREATHE, nullptr, nullptr, 0},
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
static int thermoZone = 1;
static uint8_t toolMask = TOOL_ALL;
static uint16_t accMask = ACC_DEFAULT;
static bool bridgerOn = true;
static bool expressOn = true;
static bool calmMode = false;
static bool darkMode = false;
static char teacherName[17] = "Teacher";
static char repliedChoice[40] = "";
static bool fromExpress = false;
static uint8_t copeOrder[4] = {0, 1, 2, 3};
static const uint8_t COPE_BITS[4] = {TOOL_GROUND, TOOL_FIRST, TOOL_SCHED, TOOL_BREATH};
static uint8_t lastCope = 0;
static char bridgerHint[48] = "Hi! I'm Bridger.";
static char firstText[20] = "Work";
static char thenText[20] = "Break";
static char stepText[4][20] = {"Arrive", "Learn", "Break", "Pack up"};
static int toolPage = 0;
static int firstEditSide = 0;
static int groundStep = 0;
static int seeLooks[3] = {0, 1, 2};
static const int GROUND_BTN_X = 175;
static const int GROUND_BTN_Y = 268;
static const int GROUND_BTN_W = 450;
static const int GROUND_BTN_H = 72;
static int scheduleNow = 0;
static int scheduleEdit = 0;
static bool breathFromTools = false;
static char toolEdit[20];
static int toolField = 0;
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
static bool noticeSent = false;       // kept for a failed search; Sent waits for the teacher reply
static bool sendFailed = false;       // true when the teacher radio could not be reached
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
static char classPin[5] = "";
static char settingsPin[5] = "";
static char settingsFirst[5] = "";
static bool settingsPinSet = false;
static bool classPinSet = false;
static int lockJob = LOCK_CREATE;
static int coveredScreen = SCREEN_HELLO;
static bool privacyCover = false;
static char pinEntry[5] = "";
static char lockNote[48] = "Ask your teacher for the class PIN";
static String sealedName;
static unsigned long activityMs = 0;
static bool pinRejected = false;
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
static bool exitAfterDrawingSync = false;
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
static char drawingOcrText[DRAW_OCR_MAX_BYTES + 1] = "";
static bool drawingOcrVisible = false;
static uint16_t drawingOcrPage = 0;
static uint16_t drawingOcrPageCount = 1;
static uint8_t ocrReceiveBoard = 0;
static uint32_t ocrReceiveDrawingId = 0;
static uint16_t ocrReceiveExpectedBytes = 0;
static uint16_t ocrReceiveBytes = 0;
static uint16_t ocrReceiveNextChunk = 0;
static char ocrReceiveText[DRAW_OCR_MAX_BYTES + 1] = "";
static bool ocrReceiveActive = false;
static uint32_t ocrReceiveCompletedId = 0;
static bool ocrReceiveCompletedOk = false;
static portMUX_TYPE ocrBleMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool ocrBlePacketReady = false;
static uint8_t pendingOcrBleBytes[sizeof(ClearOcrTextChunk)] = {};
static uint8_t pendingOcrBleLength = 0;
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
static bool raceRunning = false;
static bool raceDone = false;
static unsigned long raceMs = 0;
static unsigned long raceDrawMs = 0;
static int raceMark = -1;
static const unsigned long RACE_MS = 5UL * 60UL * 1000UL;
// One five-minute visit for the chalkboard and the notebook together.
// Switching between them does not start the time over.
static bool expressRunning = false;
static bool expressTimeUp = false;
static bool expressClockPaused = false;
static unsigned long expressMs = 0;
static unsigned long expressDrawMs = 0;
static unsigned long expressPauseMs = 0;
static const unsigned long EXPRESS_MS = 5UL * 60UL * 1000UL;
static bool breathOffer = false;
static const int BREATH_CYCLES = 3;
static const unsigned long BREATH_CYCLE_MS = 16000;
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

// Keep letters and spaces from a saved tool word. An empty save
// falls back to the word the panel ships with.
static void copyShort(char *dest, int size, const String &src, const char *fallback) {
  int n = 0;
  for (int i = 0; i < src.length() && n < size - 1; i++) {
    char c = src.charAt(i);
    bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    bool mark = (c == ' ' || c == '-' || c == '\'');
    if (letter || mark) {
      dest[n++] = c;
    }
  }
  dest[n] = 0;
  if (dest[0] == 0 && fallback != nullptr) {
    strncpy(dest, fallback, size - 1);
    dest[size - 1] = 0;
  }
}

// Light mode is a white page. Dark mode is a dim page, and the
// greens, purples, and oranges are deeper so they do not glow.
static uint16_t pageBg() {
  return darkMode ? 0x0841 : 0xFFFF;
}

static uint16_t pageInk() {
  return darkMode ? 0xE71C : 0x1082;
}

static uint16_t pageSoft() {
  return darkMode ? 0x8410 : 0x4A69;
}

static void applyTheme() {
  if (darkMode) {
    COL_TRY = 0x1B46;
    COL_ASK = 0xA280;
    COL_MINT = 0x1324;
    COL_CREAM = 0x3186;
    COL_LILAC = 0x30A9;
    COL_PALE = 0x2249;
  } else {
    COL_TRY = 0x07E0;
    COL_ASK = 0xFD20;
    COL_MINT = 0xE79D;
    COL_CREAM = 0xFFBD;
    COL_LILAC = 0xEF3E;
    COL_PALE = 0xE79F;
  }
}

static const char *teacherLabel() {
  return teacherName[0] ? teacherName : "Teacher";
}

// Each panel keeps its own counter, so two students do not share an id
// after a reboot. The website uses that id to attach the teacher's answer.
static uint32_t takeEventId() {
  static bool ready = false;
  if (!ready) {
    prefs.begin("clear", false);
    uint32_t saved = prefs.getUInt("evid", 0);
    prefs.end();
    if (saved >= 2) {
      nextEventId = saved;
    } else {
      nextEventId = esp_random();
      if (nextEventId < 2) {
        nextEventId = 2;
      }
    }
    ready = true;
  }
  uint32_t id = nextEventId++;
  if (nextEventId < 2) {
    nextEventId = 2;
  }
  prefs.begin("clear", false);
  prefs.putUInt("evid", nextEventId);
  prefs.end();
  return id;
}

static void rememberChoice(const char *choice) {
  if (choice == nullptr) {
    choice = "";
  }
  strncpy(repliedChoice, choice, sizeof(repliedChoice) - 1);
  repliedChoice[sizeof(repliedChoice) - 1] = 0;
}

// Read the saved name, brightness, and grade. The second argument
// to each get is what we use when nothing has been saved yet.
// The true on begin() means read-only. Names are checked character
// by character so a damaged save cannot put odd symbols on the greeting.
static void loadSettings() {
  prefs.begin("clear", true);
  String stored = prefs.getString("name", "");
  sealedName = prefs.getString("nameseal", "");
  String pin = prefs.getString("cpin", "");
  String spin = prefs.getString("spin", "");
  brightnessPct = prefs.getInt("bright", 80);
  gradeLevel = prefs.getInt("grade", GRADE_LIVE);
  toolMask = (uint8_t)prefs.getInt("tools", TOOL_ALL);
  accMask = (uint16_t)prefs.getInt("acc", ACC_DEFAULT);
  bridgerOn = prefs.getInt("beaver", 1) != 0;
  expressOn = prefs.getInt("express", 1) != 0;
  calmMode = prefs.getInt("calm", 0) != 0;
  darkMode = prefs.getInt("dark", 0) != 0;
  String tname = prefs.getString("tname", "Teacher");
  copyName(teacherName, tname.c_str());
  if (teacherName[0] == 0) {
    copyName(teacherName, "Teacher");
  }
  applyTheme();
  int packed = prefs.getInt("order", 0x3210);
  bool orderOk = true;
  bool seen[4] = {false, false, false, false};
  for (int i = 0; i < 4; i++) {
    int slot = (packed >> (i * 4)) & 0xF;
    if (slot < 0 || slot > 3 || seen[slot]) {
      orderOk = false;
    } else {
      seen[slot] = true;
      copeOrder[i] = (uint8_t)slot;
    }
  }
  if (!orderOk) {
    for (int i = 0; i < 4; i++) {
      copeOrder[i] = (uint8_t)i;
    }
  }
  accMask &= (uint16_t)((1u << ACC_COUNT) - 1);
  copyShort(firstText, sizeof(firstText), prefs.getString("first", ""), "Work");
  copyShort(thenText, sizeof(thenText), prefs.getString("then", ""), "Break");
  copyShort(stepText[0], sizeof(stepText[0]), prefs.getString("sch0", ""), "Arrive");
  copyShort(stepText[1], sizeof(stepText[1]), prefs.getString("sch1", ""), "Learn");
  copyShort(stepText[2], sizeof(stepText[2]), prefs.getString("sch2", ""), "Break");
  copyShort(stepText[3], sizeof(stepText[3]), prefs.getString("sch3", ""), "Pack up");
  prefs.end();
  classPinSet = pin.length() == 4;
  if (classPinSet) {
    for (int i = 0; i < 4; i++) {
      classPin[i] = pin.charAt(i);
    }
    classPin[4] = 0;
    for (int i = 0; i < 4; i++) {
      if (classPin[i] < '0' || classPin[i] > '9') {
        classPinSet = false;
      }
    }
  }
  settingsPinSet = spin.length() == 4;
  if (settingsPinSet) {
    for (int i = 0; i < 4; i++) {
      settingsPin[i] = spin.charAt(i);
      if (settingsPin[i] < '0' || settingsPin[i] > '9') {
        settingsPinSet = false;
      }
    }
    settingsPin[4] = 0;
  }
  if (!settingsPinSet) {
    settingsPin[0] = 0;
  }
  if (classPinSet) {
    lockJob = LOCK_ENTER;
  } else {
    lockJob = LOCK_CREATE;
    classPin[0] = 0;
  }
  if (sealedName.length() > 0) {
    stored = "";
    studentName[0] = 0;
  }

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
  if (studentName[0] == 0 && sealedName.length() == 0) {
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
  prefs.putInt("bright", brightnessPct);
  prefs.putInt("grade", gradeLevel);
  prefs.putInt("tools", toolMask);
  prefs.putInt("acc", accMask);
  prefs.putInt("beaver", bridgerOn ? 1 : 0);
  prefs.putInt("express", expressOn ? 1 : 0);
  prefs.putInt("calm", calmMode ? 1 : 0);
  prefs.putInt("dark", darkMode ? 1 : 0);
  prefs.putString("tname", teacherName);
  int packedOrder = 0;
  for (int i = 0; i < 4; i++) {
    packedOrder |= (copeOrder[i] & 0xF) << (i * 4);
  }
  prefs.putInt("order", packedOrder);
  prefs.putString("first", firstText);
  prefs.putString("then", thenText);
  prefs.putString("sch0", stepText[0]);
  prefs.putString("sch1", stepText[1]);
  prefs.putString("sch2", stepText[2]);
  prefs.putString("sch3", stepText[3]);
  if (settingsPinSet) {
    prefs.putString("spin", settingsPin);
  }
  if (classPinSet) {
    prefs.putString("cpin", classPin);
    String sealed;
    if (studentName[0] != 0 && clearSealText(classPin, String(studentName), sealed)) {
      sealedName = sealed;
      prefs.putString("nameseal", sealed);
      prefs.remove("name");
    }
  } else {
    prefs.putString("name", studentName);
  }
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
  if (len >= 2 && data[0] == CLEAR_LINK_VERSION &&
      (data[1] == CLEAR_KIND_OCR_TEXT_START ||
       data[1] == CLEAR_KIND_OCR_TEXT_CHUNK ||
       data[1] == CLEAR_KIND_OCR_TEXT_END) &&
      len <= (int)sizeof(pendingOcrBleBytes)) {
    portENTER_CRITICAL(&ocrBleMux);
    if (!ocrBlePacketReady) {
      memcpy(pendingOcrBleBytes, data, len);
      pendingOcrBleLength = len;
      ocrBlePacketReady = true;
    }
    portEXIT_CRITICAL(&ocrBleMux);
    return;
  }
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

// Neither board has a clock battery. This panel saves the time every
// minute. A reboot continues from the last save. Time while the power
// was off is filled in when the teacher device sends a newer network time.
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
    // The write may have reached the radio and the reply may not have.
    // Send the same note again. The teacher keeps one copy of that id.
    if (waitingId != 0 && teacherReply == CLEAR_ANSWER_NONE &&
        outboundNote.eventId == waitingId) {
      needSend = true;
      nextBleMs = millis();
    }
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
  // The class PIN is the Bluetooth passkey. A wrong PIN cannot join.
#if defined(CONFIG_NIMBLE_ENABLED)
  bool secured = BLESecurity::startSecurity(teacherClient->getConnId());
#else
  BLEAddress peer = teacherClient->getPeerAddress();
  bool secured = BLESecurity::startSecurity(peer.getNative());
#endif
  if (secured) {
    BLESecurity::waitForAuthenticationComplete(8000);
  }
  if (!secured) {
    teacherClient->disconnect();
    pinRejected = true;
    return false;
  }
  pinRejected = false;
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
  // A write answer means the radio took the note. The footer stays on
  // Sending until the teacher device replies that it has the note.
  bool wrote = noticeRemote->writeValue((uint8_t *)&outboundNote, sizeof(outboundNote), true);
  bool tracksStatus = waitingId != 0 && outboundNote.eventId == waitingId;
  if (wrote) {
    needSend = false;
    if (tracksStatus) {
      sendFailed = false;
      noticeSent = false;
    }
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
  sendFailed = true;
  if (currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_NAME &&
      currentScreen != SCREEN_DRAW_MENU && currentScreen != SCREEN_DRAW_CANVAS) {
    paintStatus();
  }
}

static BLEAdvertising *panelAd = nullptr;

// A teacher search stops the settings broadcast. Start it again so the
// website can still find this panel.
static void resumePanelAd() {
  if (panelAd != nullptr) {
    panelAd->start();
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
    resumePanelAd();
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
      resumePanelAd();
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

class StudentSecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override {
    return (uint32_t)atoi(classPin);
  }
  bool onSecurityRequest() override {
    return classPinSet;
  }
  bool onConfirmPIN(uint32_t pin) override {
    return pin == (uint32_t)atoi(classPin);
  }
};

static bool radioReady = false;

static void ensureRadio() {
  if (radioReady) {
    return;
  }
  BLEDevice::init("CLEAR panel");
  BLEDevice::setMTU(CLEAR_BLE_MTU);
  radioReady = true;
}

static void startTeacherLink() {
  if (linkReady || !classPinSet) {
    return;
  }
  ensureRadio();
  BLEDevice::setSecurityCallbacks(new StudentSecurityCallbacks());
  BLESecurity::setCapability(ESP_IO_CAP_IN);
  BLESecurity::setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  BLESecurity::setKeySize(16);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setPassKey(true, (uint32_t)atoi(classPin));
  linkReady = true;
}

// Keep Bridger's line short enough for the bubble beside him.
static void bridgerSay(const char *line) {
  if (line == nullptr || line[0] == 0) {
    return;
  }
  strncpy(bridgerHint, line, sizeof(bridgerHint) - 1);
  bridgerHint[sizeof(bridgerHint) - 1] = 0;
}

static const char *bridgerZoneLine() {
  if (thermoZone <= 0) {
    return "Slow and sleepy.";
  }
  if (thermoZone == 1) {
    return "Ready to learn.";
  }
  if (thermoZone == 2) {
    return "A little wiggly.";
  }
  if (thermoZone == 3) {
    return "Energy is big.";
  }
  return "Big feelings.";
}

// Bridger is a square brown beaver in the bottom-right corner.
// Ears, pink cheeks, little feet, and a crosshatch tail are drawn
// with shapes, so no picture file is stored on the panel.
static void paintBridger() {
  if (!bridgerOn || calmMode || currentScreen == SCREEN_RACE) {
    return;
  }
  const uint16_t fur = 0xC3C8;
  const uint16_t furDark = 0x5982;
  const uint16_t cheek = 0xFB2C;
  const int x = 692;
  const int y = 408;
  lcd.fillCircle(x + 14, y + 10, 9, fur);
  lcd.fillCircle(x + 46, y + 10, 9, fur);
  lcd.fillRoundRect(x, y + 12, 60, 46, 12, fur);
  lcd.fillRoundRect(x + 8, y + 52, 14, 10, 4, fur);
  lcd.fillRoundRect(x + 36, y + 52, 14, 10, 4, fur);
  lcd.fillRoundRect(x + 54, y + 26, 28, 18, 6, fur);
  lcd.drawLine(x + 58, y + 30, x + 78, y + 40, furDark);
  lcd.drawLine(x + 66, y + 30, x + 78, y + 36, furDark);
  lcd.drawLine(x + 58, y + 40, x + 76, y + 30, furDark);
  lcd.drawLine(x + 66, y + 40, x + 78, y + 32, furDark);
  lcd.fillCircle(x + 22, y + 32, 3, COL_BG);
  lcd.fillCircle(x + 38, y + 32, 3, COL_BG);
  lcd.fillCircle(x + 21, y + 31, 1, COL_FG);
  lcd.fillCircle(x + 37, y + 31, 1, COL_FG);
  lcd.fillCircle(x + 12, y + 40, 4, cheek);
  lcd.fillCircle(x + 48, y + 40, 4, cheek);
  lcd.fillCircle(x + 30, y + 38, 3, COL_BG);
  lcd.drawLine(x + 24, y + 44, x + 30, y + 47, COL_BG);
  lcd.drawLine(x + 30, y + 47, x + 36, y + 44, COL_BG);

  // The bubble hugs the words and stays next to Bridger, so the
  // status line on the left still has room.
  int bubbleW = 0;
  int bubbleX = bridgerBubbleLeft(&bubbleW);
  const uint16_t bubble = darkMode ? COL_FG : (uint16_t)0xDEDB;
  lcd.fillRoundRect(bubbleX, 416, bubbleW, 48, 16, bubble);
  lcd.fillTriangle(bubbleX + bubbleW, 430, bubbleX + bubbleW, 450, bubbleX + bubbleW + 18, 440, bubble);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_BG, bubble);
  lcd.drawString(bridgerHint, bubbleX + bubbleW / 2, 440);
  lcd.setTextDatum(textdatum_t::top_left);
}

// Left edge of Bridger's bubble. 800 means he is not on this screen.
static int bridgerBubbleLeft(int *outW) {
  if (!bridgerOn || calmMode || currentScreen == SCREEN_RACE) {
    if (outW != nullptr) {
      *outW = 0;
    }
    return 800;
  }
  lcd.setFont(&fonts::Font2);
  int textW = lcd.textWidth(bridgerHint);
  int bubbleW = textW + 36;
  if (bubbleW < 128) {
    bubbleW = 128;
  }
  if (bubbleW > 300) {
    bubbleW = 300;
  }
  int left = 676 - bubbleW;
  if (left < 248) {
    left = 248;
    bubbleW = 676 - left;
  }
  if (outW != nullptr) {
    *outW = bubbleW;
  }
  return left;
}

// Drop letters from the end until the status line stops before Bridger.
static void clipStatusLine(char *text, int maxW) {
  if (maxW < 24) {
    maxW = 24;
  }
  while (text[0] != 0 && lcd.textWidth(text) > maxW && strlen(text) > 3) {
    int n = strlen(text);
    text[n - 1] = 0;
    text[n - 2] = '.';
    text[n - 3] = '.';
    text[n - 4] = '.';
  }
}

// Redraw only the strip along the bottom. It says whether the last
// selection was sent, and then Yes or No when the teacher answers.
// Bridger, when he is on, sits in the right end of that strip.
static void paintStatus() {
  bool calm = currentScreen == SCREEN_ZONE || currentScreen == SCREEN_SOOTHE ||
              currentScreen == SCREEN_ASK || currentScreen == SCREEN_EXPRESS ||
              currentScreen == SCREEN_FEED || currentScreen == SCREEN_RACE ||
              currentScreen == SCREEN_PLAN;
  uint16_t bar = pageBg();
  uint16_t ink = pageSoft();
  uint16_t hear = darkMode ? COL_ROW : 0xDEDB;
  uint16_t hearInk = darkMode ? pageInk() : (uint16_t)0x1082;
  lcd.fillRect(0, calm ? 400 : 420, 800, calm ? 80 : 60, bar);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(ink, bar);
  lcd.setCursor(232, calm ? 424 : 440);
  // Dark mode keeps the ask cards muted. The Help button stays a brighter orange.
  const uint16_t helpFill = darkMode ? (uint16_t)0xFC63 : COL_ASK;
  lcd.fillRoundRect(8, 424, 96, 48, 10, helpFill);
  lcd.fillRoundRect(112, 424, 108, 48, 10, hear);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(0x1082, helpFill);
  lcd.drawString("Help", 56, 448);
  lcd.setTextColor(hearInk, hear);
  lcd.drawString("Hear it", 166, 448);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(ink, bar);
  char line[72];
  line[0] = 0;
  if (pinRejected) {
    snprintf(line, sizeof(line), "Class PIN does not match. Tap here to change it.");
  } else if (teacherReply == CLEAR_ANSWER_PENDING) {
    snprintf(line, sizeof(line), "Waiting for %s", teacherLabel());
  } else if (teacherReply == CLEAR_ANSWER_GOT) {
    snprintf(line, sizeof(line), "Pending");
  } else if (teacherReply == CLEAR_ANSWER_SEEN) {
    snprintf(line, sizeof(line), "Seen");
  } else if (teacherReply == CLEAR_ANSWER_YES && repliedChoice[0]) {
    snprintf(line, sizeof(line), "%s said yes to %s", teacherLabel(), repliedChoice);
  } else if (teacherReply == CLEAR_ANSWER_YES) {
    snprintf(line, sizeof(line), "%s said yes", teacherLabel());
  } else if (teacherReply == CLEAR_ANSWER_NO && repliedChoice[0]) {
    snprintf(line, sizeof(line), "%s said no to %s", teacherLabel(), repliedChoice);
  } else if (teacherReply == CLEAR_ANSWER_NO) {
    snprintf(line, sizeof(line), "%s said no", teacherLabel());
  } else if (teacherReply == CLEAR_ANSWER_LATER) {
    snprintf(line, sizeof(line), "In a minute");
  } else if (sendFailed && waitingId != 0) {
    snprintf(line, sizeof(line), "Could not send");
  } else if (waitingId != 0 && teacherReply == CLEAR_ANSWER_NONE) {
    snprintf(line, sizeof(line), "Sending");
  } else if (!linkReady) {
    snprintf(line, sizeof(line), "%s not found", teacherLabel());
  } else {
    snprintf(line, sizeof(line), "Ready");
  }
  int bubbleLeft = bridgerBubbleLeft(nullptr);
  clipStatusLine(line, bubbleLeft - 240);
  lcd.print(line);
  paintBridger();
}

// The CrowPanel speaker is an I2S amplifier on GPIO 17, 18, and 42.
// Hear it hums the instruction. Breathing adds a tone that rises and falls.
static bool audioReady = false;
static char speakText[48];
static int speakPos = 0;
static int speakSamples = 0;
static const int AUDIO_RATE = 16000;

static void startAudio() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#if defined(I2S_COMM_FORMAT_STAND_I2S)
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
#endif
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 128;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
    return;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = 42;
  pins.ws_io_num = 18;
  pins.data_out_num = 17;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    return;
  }
  audioReady = true;
}

static int toneFor(char c) {
  if (c == ' ' || c == '.' || c == ',' || c == '!' || c == '?') {
    return 0;
  }
  if (c == 'a' || c == 'A') return 440;
  if (c == 'e' || c == 'E') return 523;
  if (c == 'i' || c == 'I') return 659;
  if (c == 'o' || c == 'O') return 392;
  if (c == 'u' || c == 'U') return 330;
  return 247;
}

static void speakLine(const char *line) {
  if (line == nullptr) {
    return;
  }
  strncpy(speakText, line, sizeof(speakText) - 1);
  speakText[sizeof(speakText) - 1] = 0;
  speakPos = 0;
  speakSamples = 0;
}

static void breathGeometry(int *phase, int *count, int *size);

static void serviceAudio() {
  if (!audioReady) {
    return;
  }
  int16_t frames[128 * 2];
  int freq = 0;
  int loud = 0;
  if (speakText[0] != 0 && speakPos < (int)strlen(speakText)) {
    freq = toneFor(speakText[speakPos]);
    loud = freq == 0 ? 0 : (speakText[speakPos] >= 'a' ? 1800 : 900);
  } else if (breathRunning) {
    int phase = 0;
    int count = 0;
    int size = 0;
    breathGeometry(&phase, &count, &size);
    unsigned long along = (millis() - breathMs) % 4000;
    if (phase == 0) {
      freq = 280 + (int)(along * 220 / 4000);
    } else if (phase == 1) {
      freq = 500;
    } else if (phase == 2) {
      freq = 500 - (int)(along * 220 / 4000);
    } else {
      freq = 280;
    }
    loud = 1200;
  } else {
    return;
  }
  static float audioPhase = 0;
  for (int i = 0; i < 128; i++) {
    int16_t sample = 0;
    if (freq > 0 && loud > 0) {
      audioPhase += 2.0f * 3.14159f * freq / AUDIO_RATE;
      if (audioPhase > 6.28318f) {
        audioPhase -= 6.28318f;
      }
      sample = (int16_t)(sinf(audioPhase) * loud);
    }
    frames[i * 2] = sample;
    frames[i * 2 + 1] = sample;
    if (speakText[0] != 0) {
      speakSamples++;
      if (speakSamples >= AUDIO_RATE / 12) {
        speakSamples = 0;
        speakPos++;
        if (speakText[speakPos] == 0) {
          speakText[0] = 0;
        }
        freq = speakText[0] ? toneFor(speakText[speakPos]) : 0;
        loud = freq == 0 ? 0 : 900;
      }
    }
  }
  size_t wrote = 0;
  i2s_write(I2S_NUM_0, frames, sizeof(frames), &wrote, 0);
}

// Orange means the teacher is being asked to answer.
// Green means the student is trying something, and the teacher is told.
static bool itemAsks(const Item *item) {
  return item->action == ACT_ASK || item->action == ACT_TYPE;
}

// K and grades 1-2 blow a candle. The four counts stay the same.
static bool youngBreath() {
  return gradeLevel == GRADE_K || gradeLevel == GRADE_12;
}

// K sees the short word. Grades 1-2 see Candle blowing on a breath
// button. Older grades see the full label. The teacher notice uses
// the name of the exercise that grade actually does.
static const char *buttonLabel(const Item *item) {
  if (item->action == ACT_BREATHE && youngBreath()) {
    return gradeLevel == GRADE_K ? "Blow" : "Candle blowing";
  }
  if (gradeLevel == GRADE_K && item->little != nullptr) {
    return item->little;
  }
  return item->label;
}

static const char *breathNoticeName(const Item *item) {
  if (item->action == ACT_BREATHE && youngBreath()) {
    return "Candle blowing";
  }
  return item->label;
}

// The letters currently being typed, for whichever keyboard is open.
static char *activeText() {
  if (typeMode == TYPE_NAME || typeMode == TYPE_TEACHER) {
    return nameEdit;
  }
  if (typeMode == TYPE_TOOL) {
    return toolEdit;
  }
  return messageEdit;
}

// How many letters fit. A message with a lead has to leave room
// for those words inside the 39 characters the teacher note holds.
static int typeLimit() {
  if (typeMode == TYPE_NAME || typeMode == TYPE_TEACHER) {
    return STUDENT_NAME_MAX;
  }
  if (typeMode == TYPE_WRITE) {
    return WRITE_MAX;
  }
  if (typeMode == TYPE_TOOL) {
    return 18;
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

// A soft gear: rounded teeth, a thick ring, and a hole. The whole
// button is what a tap has to hit, not only the icon.
static void paintGearButton() {
  uint16_t btn = darkMode ? 0x39E7 : 0xDEDB;
  uint16_t metal = darkMode ? 0xBDF7 : 0x4A69;
  lcd.fillRoundRect(GEAR_X, GEAR_Y, GEAR_W, GEAR_H, 12, btn);
  int cx = GEAR_X + GEAR_W / 2;
  int cy = GEAR_Y + GEAR_H / 2;
  const int orbit = 11;
  for (int i = 0; i < 8; i++) {
    float angle = i * 3.14159f / 4.0f;
    int tx = cx + (int)(cosf(angle) * orbit);
    int ty = cy + (int)(sinf(angle) * orbit);
    lcd.fillCircle(tx, ty, 4, metal);
  }
  lcd.fillCircle(cx, cy, 8, metal);
  lcd.fillCircle(cx, cy, 4, btn);
}

// Paint one pixel of the handshake heart. Soft edges mix the green
// with the page so the stroke does not look like a block of squares.
static uint16_t blendLogo(uint16_t paper, uint8_t alpha) {
  int pr = (paper >> 11) & 31;
  int pg = (paper >> 5) & 63;
  int pb = paper & 31;
  int fr = (COL_LOGO >> 11) & 31;
  int fg = (COL_LOGO >> 5) & 63;
  int fb = COL_LOGO & 31;
  int r = pr + ((fr - pr) * alpha) / 255;
  int g = pg + ((fg - pg) * alpha) / 255;
  int b = pb + ((fb - pb) * alpha) / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static void drawLogoHeart(int x, int y) {
  uint16_t paper = pageBg();
  for (int row = 0; row < LOGO_H; row++) {
    for (int col = 0; col < LOGO_W; col++) {
      uint8_t alpha = LOGO_ALPHA[row * LOGO_W + col];
      if (alpha < 8) {
        continue;
      }
      uint16_t color = alpha > 247 ? (uint16_t)COL_LOGO : blendLogo(paper, alpha);
      lcd.drawPixel(x + col, y + row, color);
    }
  }
}

static void paintBrand(int y) {
  drawLogoHeart(4, y - 22);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageSoft(), pageBg());
  lcd.drawString("CLEAR Bridge", 56, y);
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
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(pageInk(), pageBg());
  char greeting[STUDENT_NAME_MAX + 16];
  snprintf(greeting, sizeof(greeting), "Hello, %s", studentName);
  if (gradeLevel == GRADE_K || gradeLevel == GRADE_12) {
    lcd.setFont(&fonts::Font4);
    lcd.drawString(greeting, 400, 150);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("How big are your feelings?", 400, 210);
    if (gradeLevel == GRADE_K) {
      drawFeelingFace(5, 400, 300);
    }
  } else {
    lcd.setFont(&fonts::Font4);
    lcd.drawString(greeting, 400, 170);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("How big are your feelings?", 400, 230);
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
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(pageInk(), pageBg());
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
  lcd.setTextColor(pageInk(), pageBg());
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
// In counts 1 to 4, the hold counts 4 to 1, out counts 1 to 4,
// and the last hold counts 4 to 1.
static void breathGeometry(int *phase, int *count, int *size) {
  unsigned long into = millis() - breathMs;
  int step = (int)((into / 1000) % 4);
  *phase = (int)((into / 4000) % 4);
  if (*phase == 1 || *phase == 3) {
    *count = 4 - step;
  } else {
    *count = step + 1;
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

// The candle uses the same four counts. The flame grows on the way in,
// stays tall on the hold, then leans away while the student blows.
static void paintCandleBreath(int phase, int count, int size) {
  static const char *kWords[] = {"In", "Hold", "Blow", "Hold"};
  static const char *words[] = {"Breathe in", "Hold", "Blow out", "Hold"};
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.setFont(gradeLevel == GRADE_K ? &fonts::Font4 : &fonts::Font2);
  lcd.drawString(gradeLevel == GRADE_K ? kWords[phase] : words[phase], 400, 162);

  char num[4];
  snprintf(num, sizeof(num), "%d", count);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(num, 230, 300);

  const int waxX = 388;
  const int waxTop = 292;
  lcd.fillRoundRect(waxX, waxTop, 24, 96, 6, 0xFDE0);
  lcd.fillRect(waxX + 6, waxTop + 8, 4, 72, 0xFFFF);
  lcd.fillRect(398, waxTop - 12, 4, 14, COL_FG);

  unsigned long along = (millis() - breathMs) % 4000;
  int flameH = 22 + (size - 48) * 70 / 140;
  int lean = 0;
  if (phase == 2) {
    lean = (int)(along * 64 / 4000);
  } else {
    lean = (int)((millis() / 140) % 5) - 2;
  }
  int baseX = 400;
  int baseY = waxTop - 6;
  int tipX = baseX + lean;
  int tipY = baseY - flameH;
  lcd.fillTriangle(baseX - 18, baseY + 2, baseX + 18, baseY + 2, tipX, tipY, 0xFD20);
  lcd.fillTriangle(baseX - 8, baseY, baseX + 8, baseY, tipX, baseY - (flameH * 2 / 3), 0xFFE0);
  lcd.fillCircle(baseX + lean / 5, baseY - 8, 3, COL_FG);
  if (phase == 2) {
    lcd.drawLine(280, baseY - 16, 360, baseY - 28, COL_FG);
    lcd.drawLine(270, baseY + 2, 368, baseY - 6, COL_FG);
    lcd.drawLine(286, baseY + 20, 358, baseY + 6, COL_FG);
  }
}

// Redraw only the middle of the breathing screen, so Back and the
// footer do not flicker while the picture and the count change.
static void paintBreathMotion() {
  static const char *words[] = {"Breathe in", "Hold", "Breathe out", "Hold"};
  int phase = 0;
  int count = 4;
  int size = 48;
  breathGeometry(&phase, &count, &size);
  lcd.fillRect(80, 140, 640, 260, pageBg());
  if (calmMode) {
    const char *plain = words[phase];
    if (youngBreath() && phase == 2) {
      plain = "Blow out";
    }
    char num[4];
    snprintf(num, sizeof(num), "%d", count);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(pageInk(), pageBg());
    lcd.setFont(&fonts::Font4);
    lcd.drawString(num, 400, 260);
    lcd.setFont(&fonts::Font2);
    lcd.drawString(plain, 400, 190);
    lcd.setTextDatum(textdatum_t::top_left);
    return;
  }
  if (youngBreath()) {
    paintCandleBreath(phase, count, size);
    lcd.setTextDatum(textdatum_t::top_left);
    return;
  }
  // A fixed square. The dot travels one side per count: up, across,
  // down, and back. The line it has already walked stays drawn.
  const int left = 310;
  const int top = 200;
  const int side = 180;
  const int right = left + side;
  const int bottom = top + side;
  unsigned long along = (millis() - breathMs) % 4000;
  int travel = (int)(along * side / 4000);
  if (travel > side) {
    travel = side;
  }
  int dotX = left;
  int dotY = bottom;
  if (phase == 0) {
    dotX = left;
    dotY = bottom - travel;
  } else if (phase == 1) {
    dotX = left + travel;
    dotY = top;
  } else if (phase == 2) {
    dotX = right;
    dotY = top + travel;
  } else {
    dotX = right - travel;
    dotY = bottom;
  }
  lcd.drawRect(left, top, side, side, 0x4A69);
  if (phase > 0) {
    lcd.drawLine(left, bottom, left, top, COL_TRY);
  }
  if (phase > 1) {
    lcd.drawLine(left, top, right, top, COL_TRY);
  }
  if (phase > 2) {
    lcd.drawLine(right, top, right, bottom, COL_TRY);
  }
  if (phase == 0) {
    lcd.drawLine(left, bottom, dotX, dotY, COL_TRY);
  } else if (phase == 1) {
    lcd.drawLine(left, top, dotX, dotY, COL_TRY);
  } else if (phase == 2) {
    lcd.drawLine(right, top, dotX, dotY, COL_TRY);
  } else {
    lcd.drawLine(right, bottom, dotX, dotY, COL_TRY);
  }
  lcd.fillCircle(dotX, dotY, 9, COL_FG);
  lcd.fillCircle(dotX, dotY, 4, COL_TRY);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.setFont(&fonts::Font4);
  char num[4];
  snprintf(num, sizeof(num), "%d", count);
  lcd.drawString(num, 180, 290);
  lcd.setFont(&fonts::Font2);
  lcd.drawString(words[phase], 400, 160);
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintBeginButton() {
  paintTopButton(24, 336, 752, 60, "Begin", COL_TRY);
}

// Breathing opened from the tool list. Every grade uses this page.
// Younger grades blow a candle. Older grades follow a dot around a box.
// This is not the old choice list.
static void paintBreathTool() {
  if (breathRunning) {
    lcd.fillScreen(pageBg());
    paintBackButton();
    paintBreathMotion();
    paintStatus();
    paintGearButton();
    lcd.setTextDatum(textdatum_t::top_left);
    return;
  }
  lcd.fillScreen(pageBg());
  paintBackButton();
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.setFont(&fonts::Font4);
  if (breathOffer) {
    lcd.drawString("Three breaths.", 400, 150);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("Do it again?", 400, 210);
    paintTopButton(24, 336, 752, 60, "Again", COL_TRY);
  } else if (youngBreath()) {
    lcd.drawString(gradeLevel == GRADE_K ? "Blow" : "Candle blowing", 400, 150);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("Press Begin. The flame moves on its own.", 400, 210);
    paintBeginButton();
  } else {
    lcd.drawString("Box breathing", 400, 150);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("Press Begin. Follow the dot for three boxes.", 400, 210);
    paintBeginButton();
  }
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// The detail page for one choice. Box breathing shows Begin until
// the student starts the count.
static void paintSelected() {
  if (openItem == nullptr) {
    paintList();
    return;
  }
  if (breathFromTools && openItem->action == ACT_BREATHE) {
    paintBreathTool();
    return;
  }
  if (openItem->action == ACT_BREATHE && breathRunning) {
    lcd.fillScreen(pageBg());
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
  lcd.fillScreen(pageBg());
  paintBackButton();
  lcd.fillRoundRect(24, 64, 752, 64, 12, band);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(gradeLevel == GRADE_5 ? &fonts::Font2 : &fonts::Font4);
  lcd.setTextColor(COL_FG, band);
  lcd.drawString(banner, 44, 96);
  lcd.setTextColor(pageInk(), pageBg());
  bool smallWords = (gradeLevel == GRADE_5 || gradeLevel == GRADE_K);
  bool candle = openItem->action == ACT_BREATHE && youngBreath();
  const char *title = candle ? "Candle blowing" : openItem->label;
  const char *help = openItem->help;
  if (candle && gradeLevel == GRADE_K) {
    help = "Watch the flame.\nBlow it to the side.\nPress Begin.";
  } else if (candle) {
    help = "Breathe in and the flame grows.\nBlow out and it leans away.\nCount 4, then hold.\nPress Begin.";
  }
  drawWrapped(title, 24, 148, 752, smallWords);
  if (help != nullptr) {
    drawWrapped(help, 24, 200, 752, true);
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
  lcd.fillScreen(pageBg());
  paintTopButton(NAME_BACK_X, NAME_BACK_Y, NAME_BACK_W, NAME_BACK_H, "Back", COL_ROW);
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Done", COL_TRY);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Notebook", 24, 32);
  paintExpressClock();
  for (int i = 0; i < 3; i++) {
    int top = 80 + i * 78;
    lcd.fillRoundRect(ROW_X, top, ROW_W, 68, 12, COL_CREAM);
    lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, COL_CREAM);
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
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Settings", 24, 30);
  paintTopButton(360, 8, 168, 44, "Student tools", COL_ROW);
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Done", COL_TRY);

  lcd.fillRoundRect(ROW_X, NAME_Y, ROW_W, NAME_H, 12, COL_ROW);
  lcd.fillRoundRect(ROW_X, TEACH_Y, ROW_W, TEACH_H, 12, COL_ROW);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.drawString("Student", 44, NAME_Y + NAME_H / 2);
  lcd.drawString(studentName, 160, NAME_Y + NAME_H / 2);
  lcd.drawString("Teacher", 44, TEACH_Y + TEACH_H / 2);
  lcd.drawString(teacherName, 160, TEACH_Y + TEACH_H / 2);
  paintTopButton(CHANGE_X, NAME_Y + 4, CHANGE_W, 32, "Change", COL_TRY);
  paintTopButton(CHANGE_X, TEACH_Y + 4, CHANGE_W, 32, "Change", COL_TRY);

  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
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
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString(pct, PCT_X + PCT_W / 2, BRIGHT_Y + BRIGHT_H / 2);

  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Age group", 24, GRADE_NOTE_Y);

  for (int i = 0; i < GRADE_COUNT; i++) {
    int top = gradeRowTop(i);
    bool selected = (i == gradeLevel);
    uint16_t fill = selected ? COL_TRY : COL_ROW;
    lcd.fillRoundRect(ROW_X, top, ROW_W, GRADE_H, 12, fill);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, fill);
    lcd.drawString(gradeOptions[i].label, ROW_X + 16, top + 2);
    lcd.drawString(gradeOptions[i].about, ROW_X + 16, top + 16);
    if (selected) {
      lcd.setTextDatum(textdatum_t::middle_right);
      lcd.drawString("In use", ROW_X + ROW_W - 16, top + GRADE_H / 2);
    }
  }

  lcd.fillRoundRect(ROW_X, LOOK_Y, ROW_W, LOOK_H, 12, COL_ROW);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.drawString("Look", ROW_X + 16, LOOK_Y + LOOK_H / 2);
  uint16_t lightFill = darkMode ? COL_FOOT : COL_TRY;
  uint16_t darkFill = darkMode ? COL_TRY : COL_FOOT;
  lcd.fillRoundRect(500, LOOK_Y + 4, 120, LOOK_H - 8, 8, lightFill);
  lcd.fillRoundRect(632, LOOK_Y + 4, 120, LOOK_H - 8, 8, darkFill);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(darkMode ? COL_FG : (uint16_t)0x1082, lightFill);
  lcd.drawString("Light", 560, LOOK_Y + LOOK_H / 2);
  lcd.setTextColor(darkMode ? pageInk() : COL_FG, darkFill);
  lcd.drawString("Dark", 692, LOOK_Y + LOOK_H / 2);

  lcd.fillRoundRect(ROW_X, BEAVER_Y, 368, BEAVER_H, 12, COL_ROW);
  lcd.fillRoundRect(408, BEAVER_Y, 368, BEAVER_H, 12, COL_ROW);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.drawString("Bridger", ROW_X + 16, BEAVER_Y + BEAVER_H / 2);
  lcd.drawString("Draw or write", 424, BEAVER_Y + BEAVER_H / 2);
  uint16_t beaverFill = bridgerOn ? COL_TRY : COL_FOOT;
  uint16_t expressFill = expressOn ? COL_TRY : COL_FOOT;
  lcd.fillRoundRect(250, BEAVER_Y + 8, 120, 30, 8, beaverFill);
  lcd.fillRoundRect(640, BEAVER_Y + 8, 120, 30, 8, expressFill);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(bridgerOn ? (darkMode ? pageInk() : (uint16_t)COL_BG) : (uint16_t)COL_FG, beaverFill);
  lcd.drawString(bridgerOn ? "On" : "Off", 310, BEAVER_Y + BEAVER_H / 2);
  lcd.setTextColor(expressOn ? (darkMode ? pageInk() : (uint16_t)COL_BG) : (uint16_t)COL_FG, expressFill);
  lcd.drawString(expressOn ? "On" : "Off", 700, BEAVER_Y + BEAVER_H / 2);
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
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  const char *title = typeMode == TYPE_TEACHER ? "Teacher name" : "Student name";
  const char *action = "Save";
  if (typeMode == TYPE_MESSAGE && openItem != nullptr) {
    title = openItem->label;
    action = "Send";
  } else if (typeMode == TYPE_WRITE) {
    title = "Write a line";
    action = "Save";
  } else if (typeMode == TYPE_TOOL) {
    title = "Tool words";
    action = "Save";
  }
  lcd.drawString(title, 24, 30);
  paintExpressClock();
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
  lcd.fillScreen(pageBg());
  paintTopButton(DRAW_BACK_X, DRAW_BACK_Y, DRAW_BACK_W, DRAW_BACK_H, "Back", COL_ROW);

  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Graph board, White Board, or Chalk board?", 400, 30);
  paintExpressClock();

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
  return selectedBoard == BOARD_CHALK ? 0x13A4 : 0x0000;
}

static int paintDrawingOcrTextPage() {
  const int left = 18;
  const int right = 782;
  const int firstY = DRAW_HEADER_H + 12;
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  int lineHeight = lcd.fontHeight() + 8;
  int linesPerPage = (DRAW_CANVAS_BOTTOM - firstY - 8) / lineHeight;
  int line = 0;
  int cursorX = left;
  int spaceWidth = lcd.textWidth(" ");
  const char *word = drawingOcrText;
  while (*word) {
    if (*word == '\n') {
      line++;
      cursorX = left;
      word++;
      continue;
    }
    while (*word == ' ') word++;
    if (!*word) break;
    const char *end = word;
    while (*end && *end != ' ' && *end != '\n') end++;
    char buffer[80];
    int length = end - word;
    if (length > (int)sizeof(buffer) - 1) length = sizeof(buffer) - 1;
    memcpy(buffer, word, length);
    buffer[length] = 0;
    int width = lcd.textWidth(buffer);
    if (cursorX > left && cursorX + width > right) {
      line++;
      cursorX = left;
    }
    if (line >= (int)drawingOcrPage * linesPerPage &&
        line < ((int)drawingOcrPage + 1) * linesPerPage) {
      lcd.drawString(buffer, cursorX,
                     firstY + (line - drawingOcrPage * linesPerPage) * lineHeight);
    }
    cursorX += width + spaceWidth;
    word = end;
  }
  int pageCount = line / linesPerPage + 1;
  drawingOcrPageCount = pageCount;
  if (drawingOcrPage >= drawingOcrPageCount) {
    drawingOcrPage = drawingOcrPageCount - 1;
    return paintDrawingOcrTextPage();
  }
  return pageCount;
}

// Only the chips are redrawn when the student picks a new chalk color.
// Strokes already on the board keep the color they were drawn with.
static void paintChalkChoices() {
  lcd.fillRect(0, DRAW_COLOR_Y - 2, 800, DRAW_COLOR_H + 6, 0x13A4);
  for (int i = 0; i < DRAW_COLOR_COUNT; i++) {
    lcd.fillRoundRect(DRAW_COLOR_X[i], DRAW_COLOR_Y, DRAW_COLOR_W, DRAW_COLOR_H,
                      8, DRAW_CHALK_COLORS[i]);
    lcd.drawRoundRect(DRAW_COLOR_X[i], DRAW_COLOR_Y, DRAW_COLOR_W, DRAW_COLOR_H,
                      8, i == selectedChalkColor ? COL_FG : (uint16_t)0x13A4);
    if (i == selectedChalkColor) {
      lcd.drawRect(DRAW_COLOR_X[i] + 2, DRAW_COLOR_Y + 2,
                   DRAW_COLOR_W - 4, DRAW_COLOR_H - 4, COL_FG);
    }
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(DRAW_CHALK_INK[i], DRAW_CHALK_COLORS[i]);
    lcd.drawString(DRAW_CHALK_NAMES[i], DRAW_COLOR_X[i] + DRAW_COLOR_W / 2,
                   DRAW_COLOR_Y + DRAW_COLOR_H / 2);
  }
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintDrawingCanvas() {
  uint16_t surface = drawingSurfaceColor();
  uint16_t header = selectedBoard == BOARD_CHALK ? 0x13A4 : COL_BG;
  lcd.fillScreen(surface);
  lcd.fillRect(0, 0, 800, DRAW_HEADER_H, header);

  if (!drawingOcrVisible && selectedBoard == BOARD_GRAPH) {
        const uint16_t gridColor = 0x2104;
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
  }

  uint16_t buttonFill = selectedBoard == BOARD_CHALK ? 0xFFFF : COL_ROW;
  paintTopButton(DRAW_BACK_X, DRAW_BUTTON_Y, DRAW_BACK_W, DRAW_BUTTON_H, "Back", buttonFill);
  paintTopButton(DRAW_SAVE_X, DRAW_BUTTON_Y, 80, DRAW_BUTTON_H, "Save", buttonFill);
  paintTopButton(DRAW_RESET_X, DRAW_BUTTON_Y, 104, DRAW_BUTTON_H, "Reset", buttonFill);
  paintTopButton(DRAW_NEW_X, DRAW_BUTTON_Y, 96, DRAW_BUTTON_H, "New", buttonFill);
  paintTopButton(DRAW_READ_X, DRAW_BUTTON_Y, 88, DRAW_BUTTON_H,
                 drawingOcrVisible ? "Draw" : "Read", buttonFill);
  if (selectedBoard == BOARD_CHALK) {
    paintChalkChoices();
  }

  if (drawingOcrVisible) {
    lcd.fillRect(0, DRAW_HEADER_H, 800,
                 DRAW_CANVAS_BOTTOM - DRAW_HEADER_H, COL_BG);
    int pageCount = paintDrawingOcrTextPage();
    lcd.fillRect(0, DRAW_CANVAS_BOTTOM, 800, 480 - DRAW_CANVAS_BOTTOM, COL_FOOT);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, COL_FOOT);
    char pageStatus[64];
    snprintf(pageStatus, sizeof(pageStatus),
             "Readable text - page %u/%d. Tap below to continue",
             (unsigned)(drawingOcrPage + 1), pageCount);
    lcd.drawString(pageStatus, 18, DRAW_CANVAS_BOTTOM + 30);
    paintExpressClock();
    lcd.setTextDatum(textdatum_t::top_left);
    cursorVisible = false;
    return;
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
  paintExpressClock();
  lcd.setTextDatum(textdatum_t::top_left);
  cursorVisible = false;
}

static uint16_t drawingInkColor() {
  if (selectedBoard == BOARD_GRAPH) {
     return 0xFFFF;;
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

static const char *drawingOcrPath(int board) {
  if (board == BOARD_GRAPH) return "/graph.ocr";
  if (board == BOARD_WHITE) return "/white.ocr";
  return "/chalk.ocr";
}

static void setDrawStatus(const char *text, unsigned long durationMs = 0);

static void clearDrawingOcr(int board) {
  if (drawStorageReady && SPIFFS.exists(drawingOcrPath(board)) &&
      !SPIFFS.remove(drawingOcrPath(board))) {
    setDrawStatus("Could not remove outdated readable text");
  }
  if (selectedBoard == board) {
    drawingOcrText[0] = 0;
    drawingOcrVisible = false;
    drawingOcrPage = 0;
    drawingOcrPageCount = 1;
  }
}

static bool loadDrawingOcr(int board) {
  drawingOcrText[0] = 0;
  drawingOcrVisible = false;
  drawingOcrPage = 0;
  drawingOcrPageCount = 1;
  if (!drawStorageReady) return false;
  File file = SPIFFS.open(drawingOcrPath(board), FILE_READ);
  if (!file) return false;
  DrawingOcrFileHeader header = {};
  bool valid = file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) &&
               header.magic == DRAW_OCR_FILE_MAGIC &&
               header.drawingId == currentDrawingId &&
               header.textBytes > 0 &&
               header.textBytes <= DRAW_OCR_MAX_BYTES &&
               file.size() == sizeof(header) + header.textBytes &&
               file.read((uint8_t *)drawingOcrText, header.textBytes) ==
                 header.textBytes;
  file.close();
  if (!valid) {
    SPIFFS.remove(drawingOcrPath(board));
    drawingOcrText[0] = 0;
    return false;
  }
  drawingOcrText[header.textBytes] = 0;
  for (size_t i = 0; i < header.textBytes; i++) {
    if (drawingOcrText[i] == 0) {
      SPIFFS.remove(drawingOcrPath(board));
      drawingOcrText[0] = 0;
      return false;
    }
  }
  return true;
}

static bool saveDrawingOcr(uint8_t board, uint32_t drawingId,
                           const char *text, uint16_t size) {
  if (!drawStorageReady || board > BOARD_CHALK || size == 0 ||
      size > DRAW_OCR_MAX_BYTES) {
    return false;
  }
  File drawing = SPIFFS.open(drawingFilePath(board), FILE_READ);
  if (!drawing) return false;
  DrawingFileHeader header = {};
  bool matches = drawing.read((uint8_t *)&header, sizeof(header)) == sizeof(header) &&
                 header.magic == DRAW_FILE_MAGIC &&
                 header.version == DRAW_FILE_VERSION &&
                 header.board == board && header.drawingId == drawingId &&
                 header.segmentCount <= DRAW_SEGMENT_MAX &&
                 drawing.size() == sizeof(header) +
                     (size_t)header.segmentCount * sizeof(DrawingSegment);
  drawing.close();
  if (!matches) return false;

  const char *path = drawingOcrPath(board);
  const char *tempPath = "/readable.tmp";
  const char *backupPath = "/readable.bak";
  File output = SPIFFS.open(tempPath, FILE_WRITE);
  if (!output) return false;
  DrawingOcrFileHeader ocrHeader = {};
  ocrHeader.magic = DRAW_OCR_FILE_MAGIC;
  ocrHeader.drawingId = drawingId;
  ocrHeader.textBytes = size;
  size_t written = output.write((const uint8_t *)&ocrHeader, sizeof(ocrHeader));
  written += output.write((const uint8_t *)text, size);
  output.flush();
  output.close();
  if (written != sizeof(ocrHeader) + size) {
    SPIFFS.remove(tempPath);
    return false;
  }

  SPIFFS.remove(backupPath);
  bool hadPrevious = SPIFFS.exists(path);
  bool movedPrevious = !hadPrevious || SPIFFS.rename(path, backupPath);
  bool replaced = movedPrevious && SPIFFS.rename(tempPath, path);
  if (!replaced) {
    if (hadPrevious && movedPrevious) SPIFFS.rename(backupPath, path);
    SPIFFS.remove(tempPath);
    return false;
  }
  SPIFFS.remove(backupPath);
  return true;
}

static const char *drawingBoardName(int board) {
  if (board == BOARD_GRAPH) return "graph";
  if (board == BOARD_WHITE) return "whiteboard";
  return "chalkboard";
}

// The chalkboard, the notebook, and the page that picks them.
static bool expressCountsNow() {
  if (currentScreen == SCREEN_EXPRESS || currentScreen == SCREEN_WRITE ||
      currentScreen == SCREEN_DRAW_CANVAS || currentScreen == SCREEN_DRAW_MENU) {
    return true;
  }
  return currentScreen == SCREEN_NAME && typeMode == TYPE_WRITE;
}

static unsigned long expressLeft() {
  if (!expressRunning) {
    return EXPRESS_MS;
  }
  unsigned long gone = millis() - expressMs;
  if (gone >= EXPRESS_MS) {
    return 0;
  }
  return EXPRESS_MS - gone;
}

// A small clock so the five minutes are visible while drawing or writing.
static void paintExpressClock() {
  if (!expressRunning) {
    return;
  }
  unsigned long left = expressLeft();
  char clock[8];
  snprintf(clock, sizeof(clock), "%d:%02d", (int)(left / 60000UL),
           (int)((left / 1000UL) % 60UL));
  if (currentScreen == SCREEN_DRAW_CANVAS && selectedBoard == BOARD_CHALK) {
    lcd.fillRoundRect(624, 8, 164, 44, 10, COL_FG);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(0x1082, COL_FG);
    lcd.drawString(clock, 706, 30);
  } else if (currentScreen == SCREEN_DRAW_CANVAS) {
    lcd.fillRect(640, DRAW_CANVAS_BOTTOM, 160, 60, COL_FOOT);
    lcd.setTextDatum(textdatum_t::middle_right);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, COL_FOOT);
    lcd.drawString(clock, 784, DRAW_CANVAS_BOTTOM + 30);
  } else if (currentScreen == SCREEN_DRAW_MENU) {
    lcd.fillRect(680, 8, 110, 44, pageBg());
    lcd.setTextDatum(textdatum_t::middle_right);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(pageInk(), pageBg());
    lcd.drawString(clock, 784, 30);
  } else if (currentScreen == SCREEN_NAME || currentScreen == SCREEN_WRITE) {
    lcd.fillRect(200, 8, 140, 44, pageBg());
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(pageInk(), pageBg());
    lcd.drawString(clock, 200, currentScreen == SCREEN_WRITE ? 32 : 30);
  } else {
    lcd.fillRect(430, 48, 140, 44, pageBg());
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(pageInk(), pageBg());
    lcd.drawString(clock, 430, 70);
  }
  lcd.setTextDatum(textdatum_t::top_left);
}

static void setDrawStatus(const char *text, unsigned long durationMs) {
  copyField(drawStatus, sizeof(drawStatus), text);
  drawStatusUntilMs = durationMs ? millis() + durationMs : 0;
  if (currentScreen == SCREEN_DRAW_CANVAS) {
    lcd.fillRect(0, DRAW_CANVAS_BOTTOM, 800, 480 - DRAW_CANVAS_BOTTOM, COL_FOOT);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, COL_FOOT);
    lcd.drawString(drawStatus, 18, DRAW_CANVAS_BOTTOM + 30);
    paintExpressClock();
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
  if (SPIFFS.exists(drawingOcrPath(oldestBoard)) &&
      !SPIFFS.remove(drawingOcrPath(oldestBoard))) {
    Serial.println("Could not remove OCR text for an evicted drawing");
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

static void sendOcrTextAck(uint32_t drawingId, uint16_t chunkIndex,
                           bool accepted) {
  ClearOcrTextAck ack = {};
  ack.version = CLEAR_LINK_VERSION;
  ack.kind = CLEAR_KIND_OCR_TEXT_ACK;
  ack.drawingId = drawingId;
  ack.chunkIndex = chunkIndex;
  ack.accepted = accepted ? 1 : 0;
  if (!sendDrawingPacket((const uint8_t *)&ack, sizeof(ack))) {
    Serial.println("OCR text acknowledgement could not be sent");
  }
}

static void processOcrTextPacket(const uint8_t *data, size_t length) {
  if (length < 2 || data[0] != CLEAR_LINK_VERSION) return;
  if (data[1] == CLEAR_KIND_OCR_TEXT_START &&
      length == sizeof(ClearOcrTextStart)) {
    ClearOcrTextStart start = {};
    memcpy(&start, data, sizeof(start));
    bool accepted = start.board <= BOARD_CHALK &&
                    start.totalBytes > 0 &&
                    start.totalBytes <= DRAW_OCR_MAX_BYTES;
    ocrReceiveActive = accepted;
    ocrReceiveBoard = start.board;
    ocrReceiveDrawingId = start.drawingId;
    ocrReceiveExpectedBytes = accepted ? start.totalBytes : 0;
    ocrReceiveBytes = 0;
    ocrReceiveNextChunk = 0;
    ocrReceiveText[0] = 0;
    sendOcrTextAck(start.drawingId, 0xFFFF, accepted);
    return;
  }

  if (data[1] == CLEAR_KIND_OCR_TEXT_CHUNK &&
      length >= offsetof(ClearOcrTextChunk, payload)) {
    ClearOcrTextChunk chunk = {};
    size_t headerSize = offsetof(ClearOcrTextChunk, payload);
    memcpy(&chunk, data, length < sizeof(chunk) ? length : sizeof(chunk));
    bool shapeValid = chunk.payloadSize > 0 &&
                      chunk.payloadSize <= CLEAR_DRAWING_CHUNK_SIZE &&
                      length == headerSize + chunk.payloadSize;
    bool accepted = ocrReceiveActive && shapeValid &&
                    chunk.drawingId == ocrReceiveDrawingId;
    if (accepted && chunk.chunkIndex < ocrReceiveNextChunk) {
      size_t offset = (size_t)chunk.chunkIndex * CLEAR_DRAWING_CHUNK_SIZE;
      accepted = offset + chunk.payloadSize <= ocrReceiveBytes &&
                 memcmp(ocrReceiveText + offset, chunk.payload,
                        chunk.payloadSize) == 0;
    } else if (accepted && chunk.chunkIndex == ocrReceiveNextChunk) {
      accepted = (size_t)ocrReceiveBytes + chunk.payloadSize <=
                 ocrReceiveExpectedBytes;
      if (accepted) {
        memcpy(ocrReceiveText + ocrReceiveBytes, chunk.payload,
               chunk.payloadSize);
        ocrReceiveBytes += chunk.payloadSize;
        ocrReceiveNextChunk++;
      }
    } else if (accepted) {
      accepted = false;
    }
    sendOcrTextAck(chunk.drawingId, chunk.chunkIndex, accepted);
    return;
  }

  if (data[1] == CLEAR_KIND_OCR_TEXT_END &&
      length == sizeof(ClearOcrTextEnd)) {
    ClearOcrTextEnd end = {};
    memcpy(&end, data, sizeof(end));
    bool accepted = ocrReceiveActive &&
                    end.drawingId == ocrReceiveDrawingId &&
                    end.chunkCount == ocrReceiveNextChunk &&
                    ocrReceiveBytes == ocrReceiveExpectedBytes;
    if (accepted) {
      ocrReceiveText[ocrReceiveBytes] = 0;
      for (uint16_t i = 0; i < ocrReceiveBytes; i++) {
        if (ocrReceiveText[i] == 0) accepted = false;
      }
    }
    if (accepted) {
      accepted = saveDrawingOcr(ocrReceiveBoard, ocrReceiveDrawingId,
                                ocrReceiveText, ocrReceiveBytes);
    }
    ocrReceiveActive = false;
    if (accepted && selectedBoard == ocrReceiveBoard &&
        currentDrawingId == ocrReceiveDrawingId) {
      memcpy(drawingOcrText, ocrReceiveText, ocrReceiveBytes + 1);
      drawingOcrVisible = false;
      drawingOcrPage = 0;
      drawingOcrPageCount = 1;
      if (currentScreen == SCREEN_DRAW_CANVAS) {
        setDrawStatus("Readable text received. Tap Read to view it");
      }
    }
    if (!accepted) {
      Serial.println("OCR text was rejected or could not be saved");
    }
    sendOcrTextAck(end.drawingId, 0xFFFE, accepted);
  }
}

static void serviceOcrTextPacket() {
  uint8_t packet[sizeof(ClearOcrTextChunk)] = {};
  uint8_t length = 0;
  portENTER_CRITICAL(&ocrBleMux);
  if (ocrBlePacketReady) {
    length = pendingOcrBleLength;
    memcpy(packet, pendingOcrBleBytes, length);
    ocrBlePacketReady = false;
    pendingOcrBleLength = 0;
  }
  portEXIT_CRITICAL(&ocrBleMux);
  if (length) processOcrTextPacket(packet, length);
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
    if (expressTimeUp) {
      expressTimeUp = false;
      exitAfterDrawingSync = false;
      expressRunning = false;
      fromExpress = false;
      bridgerSay("Five minutes. Time for a break.");
      currentScreen = SCREEN_ZONE;
      paintZone();
      return;
    }
    exitAfterDrawingSync = false;
    setDrawStatus("Teacher card could not save; tap Save to retry");
    return;
  }
  setDrawStatus("Drawing saved to teacher card");
  if (newBoardAfterSync) {
    newBoardAfterSync = false;
    clearDrawingOcr(selectedBoard);
    drawingSegmentCount = 0;
    currentDrawingId = newDrawingId();
    cursorVisible = false;
    saveDrawingFile();
    paintDrawingCanvas();
    setDrawStatus("New board ready");
  }
  if (expressTimeUp) {
    expressTimeUp = false;
    exitAfterDrawingSync = false;
    expressRunning = false;
    fromExpress = false;
    bridgerSay("Five minutes. Time for a break.");
    currentScreen = SCREEN_ZONE;
    paintZone();
    return;
  }
  if (exitAfterDrawingSync) {
    exitAfterDrawingSync = false;
    if (fromExpress) {
      fromExpress = false;
      currentScreen = SCREEN_EXPRESS;
      paintExpress();
    } else {
      openDrawingMenu();
    }
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

// Bright bands use black writing. Blue and red use white.
static uint16_t zoneInk(int zone) {
  if (zone == 1 || zone == 2 || zone == 3) {
    return COL_BG;
  }
  return COL_FG;
}

static bool toolOn(uint8_t bit) {
  return (toolMask & bit) != 0;
}

static int zoneBandHeight() {
  return (TUBE_BOTTOM - TUBE_TOP) / ZONE_COUNT;
}

static int zoneBandTop(int zone) {
  return TUBE_BOTTOM - (zone + 1) * zoneBandHeight();
}

// Finger height on the tube becomes a zone. The bottom of the tube is blue.
static int zoneFromY(int y) {
  int bandH = zoneBandHeight();
  if (bandH < 1) {
    return 0;
  }
  if (y >= TUBE_BOTTOM) {
    return 0;
  }
  if (y <= TUBE_TOP) {
    return ZONE_COUNT - 1;
  }
  int zone = (TUBE_BOTTOM - y) / bandH;
  if (zone >= ZONE_COUNT) {
    zone = ZONE_COUNT - 1;
  }
  return zone;
}

static bool fingerOnTube(int x, int y) {
  int cx = TUBE_X + TUBE_W / 2;
  int bulbY = TUBE_BOTTOM + 16;
  int dx = x - cx;
  int dy = y - bulbY;
  if (dx * dx + dy * dy <= (BULB_R + 18) * (BULB_R + 18)) {
    return true;
  }
  return x >= TUBE_X - 36 && x <= TUBE_X + TUBE_W + 28 &&
         y >= TUBE_TOP - 28 && y <= TUBE_BOTTOM + 8;
}

// The first screen after the greeting. The tube is one rounded glass
// column. The student slides a zone, then presses This is me.
static void paintThermometer() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.setFont(&fonts::Font4);
  lcd.drawString("How big are your feelings?", 24, 16);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("How much energy are you using?", 24, 52);

  int cx = TUBE_X + TUBE_W / 2;
  int tubeH = TUBE_BOTTOM - TUBE_TOP;
  int radius = TUBE_W / 2;
  lcd.fillRoundRect(TUBE_X, TUBE_TOP, TUBE_W, tubeH, radius, feelingZones[ZONE_COUNT - 1].color);
  for (int zone = 0; zone < ZONE_COUNT - 1; zone++) {
    int top = zoneBandTop(zone);
    int h = (zone == 0) ? (TUBE_BOTTOM - top) : zoneBandHeight();
    lcd.fillRect(TUBE_X, top, TUBE_W, h, feelingZones[zone].color);
  }
  int bulbY = TUBE_BOTTOM + BULB_R - 16;
  lcd.drawRoundRect(TUBE_X - 3, TUBE_TOP - 3, TUBE_W + 6, tubeH + 6, radius + 3, pageInk());
  lcd.fillCircle(cx, bulbY, BULB_R, feelingZones[0].color);
  lcd.fillRect(TUBE_X, TUBE_BOTTOM - 8, TUBE_W, 16, feelingZones[0].color);
  lcd.drawFastVLine(TUBE_X - 3, TUBE_TOP + radius, TUBE_BOTTOM - TUBE_TOP - radius, pageInk());
  lcd.drawFastVLine(TUBE_X + TUBE_W + 2, TUBE_TOP + radius, TUBE_BOTTOM - TUBE_TOP - radius, pageInk());
  lcd.drawCircle(cx, bulbY, BULB_R, pageInk());
  lcd.drawCircle(cx, bulbY, BULB_R - 1, pageInk());

  for (int y = TUBE_TOP + 14; y < TUBE_BOTTOM - 4; y += 8) {
    int len = (((y - TUBE_TOP) / 8) % 3 == 0) ? 16 : 8;
    lcd.drawFastHLine(TUBE_X + TUBE_W + 8, y, len, pageInk());
  }

  for (int zone = 0; zone < ZONE_COUNT; zone++) {
    int top = zoneBandTop(zone);
    int mid = top + zoneBandHeight() / 2;
    int boxY = mid - LABEL_H / 2;
    bool selected = zone == thermoZone;
    uint16_t fill = selected ? feelingZones[zone].color : pageBg();
    uint16_t ink = selected ? zoneInk(zone) : pageInk();
    lcd.drawLine(TUBE_X + TUBE_W + 4, mid, LABEL_X, boxY + LABEL_H / 2, pageInk());
    lcd.fillRoundRect(LABEL_X, boxY, LABEL_W, LABEL_H, 12, fill);
    lcd.drawRoundRect(LABEL_X, boxY, LABEL_W, LABEL_H, 12, pageInk());
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(ink, fill);
    lcd.drawString(feelingZones[zone].name, LABEL_X + LABEL_W / 2, boxY + LABEL_H / 2);
  }

  uint16_t button = feelingZones[thermoZone].color;
  lcd.fillRoundRect(THIS_X, THIS_Y, THIS_W, THIS_H, 12, button);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(zoneInk(thermoZone), button);
  lcd.drawString("This is me", THIS_X + THIS_W / 2, THIS_Y + THIS_H / 2);

  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static const int SOOTHE_ROW_H = 44;

static int toolRowY(int index) {
  return 124 + index * 50;
}

static int copeShown() {
  int shown = 0;
  for (int i = 0; i < 4; i++) {
    if (toolOn(COPE_BITS[copeOrder[i]])) {
      shown++;
    }
  }
  return shown;
}

static const char *breathToolName() {
  return "Breathe";
}

static void drawHeart(int cx, int cy) {
  lcd.fillCircle(cx - 8, cy - 2, 8, COL_ICON);
  lcd.fillCircle(cx + 8, cy - 2, 8, COL_ICON);
  lcd.fillTriangle(cx - 16, cy + 1, cx + 16, cy + 1, cx, cy + 18, COL_ICON);
}

static void drawHand(int cx, int cy) {
  uint16_t ink = darkMode ? pageInk() : COL_BG;
  lcd.fillRoundRect(cx - 12, cy, 24, 16, 6, ink);
  lcd.fillRoundRect(cx - 10, cy - 14, 5, 16, 2, ink);
  lcd.fillRoundRect(cx - 3, cy - 18, 5, 20, 2, ink);
  lcd.fillRoundRect(cx + 4, cy - 14, 5, 16, 2, ink);
}

// The Tools card uses the briefcase picture. Soft edges mix with the
// card color so the outline stays smooth.
static void drawToolbox(int cx, int cy, uint16_t paper) {
  uint16_t ink = darkMode ? pageInk() : (uint16_t)0x1082;
  int left = cx - TOOLBOX_W / 2;
  int top = cy - TOOLBOX_H / 2;
  int ir = (ink >> 11) & 31;
  int ig = (ink >> 5) & 63;
  int ib = ink & 31;
  int pr = (paper >> 11) & 31;
  int pg = (paper >> 5) & 63;
  int pb = paper & 31;
  for (int row = 0; row < TOOLBOX_H; row++) {
    for (int col = 0; col < TOOLBOX_W; col++) {
      uint8_t alpha = TOOLBOX_ALPHA[row * TOOLBOX_W + col];
      if (alpha < 8) {
        continue;
      }
      uint16_t color = ink;
      if (alpha < 248) {
        int r = pr + ((ir - pr) * alpha) / 255;
        int g = pg + ((ig - pg) * alpha) / 255;
        int b = pb + ((ib - pb) * alpha) / 255;
        color = (uint16_t)((r << 11) | (g << 5) | b);
      }
      lcd.drawPixel(left + col, top + row, color);
    }
  }
}

static void drawPencil(int cx, int cy) {
  lcd.fillRoundRect(cx - 4, cy - 18, 8, 28, 2, darkMode ? pageInk() : COL_BG);
  lcd.fillRect(cx - 4, cy - 18, 8, 5, 0xF518);
  lcd.fillTriangle(cx - 4, cy + 10, cx + 4, cy + 10, cx, cy + 20, COL_ASK);
}

static void drawCheck(int cx, int cy) {
  lcd.drawCircle(cx, cy, 16, COL_ICON);
  lcd.drawCircle(cx, cy, 15, COL_ICON);
  lcd.drawLine(cx - 8, cy, cx - 2, cy + 7, COL_ICON);
  lcd.drawLine(cx - 2, cy + 7, cx + 9, cy - 7, COL_ICON);
  lcd.drawLine(cx - 8, cy + 1, cx - 2, cy + 8, COL_ICON);
  lcd.drawLine(cx - 2, cy + 8, cx + 9, cy - 6, COL_ICON);
}

static void drawRedo(int cx, int cy) {
  lcd.drawCircle(cx, cy, 14, COL_BG);
  lcd.drawCircle(cx, cy, 13, COL_BG);
  lcd.fillRect(cx + 2, cy - 18, 16, 10, COL_PALE);
  lcd.fillTriangle(cx + 8, cy - 22, cx + 8, cy - 6, cx + 20, cy - 14, COL_BG);
}

static void drawSoftX(int cx, int cy) {
  lcd.drawCircle(cx, cy, 16, 0xC468);
  lcd.drawCircle(cx, cy, 15, 0xC468);
  lcd.drawLine(cx - 7, cy - 7, cx + 7, cy + 7, 0xC468);
  lcd.drawLine(cx - 7, cy + 7, cx + 7, cy - 7, 0xC468);
  lcd.drawLine(cx - 6, cy - 7, cx + 7, cy + 6, 0xC468);
  lcd.drawLine(cx - 6, cy + 7, cx + 7, cy - 6, 0xC468);
}

static const int HUB_Y1 = 128;
static const int HUB_Y2 = 260;
static const int HUB_H = 120;
static const int HUB_W = 372;

static void paintHubCard(int x, int y, int w, int h, uint16_t fill, const char *title, const char *about) {
  lcd.fillRoundRect(x, y, w, h, 22, fill);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, fill);
  lcd.drawString(title, x + w / 2, y + 72);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(darkMode ? (uint16_t)0xB596 : (uint16_t)0x4A69, fill);
  lcd.drawString(about, x + w / 2, y + 98);
}

// The page after the thermometer. Four soft cards. Draw or write
// can be turned off, and the tools card stays.
static void paintZone() {
  lcd.fillScreen(pageBg());
  char hello[STUDENT_NAME_MAX + 12];
  snprintf(hello, sizeof(hello), "Hello, %s!", studentName);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  paintBrand(28);
  lcd.drawString(hello, 24, 64);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("What would help you right now?", 24, 104);

  paintHubCard(20, HUB_Y1, HUB_W, HUB_H, softFill(COL_MINT), "Feel Better", "Breathing and calming tools");
  if (!calmMode) drawHeart(206, HUB_Y1 + 28);
  paintHubCard(408, HUB_Y1, HUB_W, HUB_H, softFill(COL_CREAM), "Ask for Help", "Tell your teacher what you need");
  if (!calmMode) drawHand(594, HUB_Y1 + 28);
  if (expressOn) {
    paintHubCard(20, HUB_Y2, HUB_W, HUB_H, softFill(COL_LILAC), "Draw or Write", "Express yourself another way");
    if (!calmMode) drawPencil(206, HUB_Y2 + 28);
    paintHubCard(408, HUB_Y2, HUB_W, HUB_H, softFill(COL_PALE), "Tools", "Race, schedule, and first then");
    if (!calmMode) drawToolbox(408 + HUB_W / 2, HUB_Y2 + 28, softFill(COL_PALE));
  } else {
    paintHubCard(20, HUB_Y2, 760, HUB_H, softFill(COL_PALE), "Tools", "Race, schedule, and first then");
    if (!calmMode) drawToolbox(400, HUB_Y2 + 28, softFill(COL_PALE));
  }
  if (toolOn(TOOL_ZONES)) {
    paintBackButton();
  }
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// Coping tools the teacher left on. This is Feel Better.
static void paintSoothe() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Feel Better", 24, 70);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageSoft(), pageBg());
  lcd.drawString("Breathing and calming tools", 24, 102);
  int shown = 0;
  lcd.setFont(&fonts::Font4);
  for (int i = 0; i < 4; i++) {
    uint8_t bit = COPE_BITS[copeOrder[i]];
    if (!toolOn(bit)) {
      continue;
    }
    if (bit == TOOL_FIRST || bit == TOOL_SCHED) {
      continue;
    }
    const char *name = bit == TOOL_GROUND ? "See, hear, feel" : "Breathing";
    int top = toolRowY(shown);
    lcd.fillRoundRect(24, top, 752, SOOTHE_ROW_H, 16, softFill(COL_MINT));
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, softFill(COL_MINT));
    lcd.drawString(name, 44, top + SOOTHE_ROW_H / 2);
    shown++;
  }
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// Race Bridger, the schedule, and First, then.
static void paintPlan() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Tools", 24, 70);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageSoft(), pageBg());
  lcd.drawString("Race, schedule, and first then", 24, 102);
  int shown = 0;
  lcd.setFont(&fonts::Font4);
  for (int i = 0; i < 4; i++) {
    uint8_t bit = COPE_BITS[copeOrder[i]];
    if (bit != TOOL_FIRST && bit != TOOL_SCHED) {
      continue;
    }
    if (!toolOn(bit)) {
      continue;
    }
    const char *name = bit == TOOL_FIRST ? "First, then" : "Schedule";
    int top = toolRowY(shown);
    lcd.fillRoundRect(24, top, 752, SOOTHE_ROW_H, 16, softFill(COL_PALE));
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, softFill(COL_PALE));
    lcd.drawString(name, 44, top + SOOTHE_ROW_H / 2);
    shown++;
  }
  int raceTop = toolRowY(shown);
  lcd.fillRoundRect(24, raceTop, 752, SOOTHE_ROW_H, 16, softFill(COL_PALE));
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, softFill(COL_PALE));
  lcd.drawString("Race Bridger", 44, raceTop + SOOTHE_ROW_H / 2);
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// How far Bridger has walked. Before the start he stands at the left.
// After five minutes he stays at the pond.
static unsigned long raceGone() {
  if (raceDone) {
    return RACE_MS;
  }
  if (!raceRunning) {
    return 0;
  }
  unsigned long gone = millis() - raceMs;
  if (gone > RACE_MS) {
    return RACE_MS;
  }
  return gone;
}

static int raceWalkerX() {
  return 56 + (int)((raceGone() * 600UL) / RACE_MS);
}

// A small Bridger for the path. The corner Bridger stays off this page
// so there is only one of him, and he does not bounce around.
static void drawTrackBridger(int x, int y) {
  if (calmMode) {
    lcd.fillCircle(x + 16, y + 18, 14, 0xC3C8);
    return;
  }
  const uint16_t fur = 0xC3C8;
  lcd.fillCircle(x + 8, y + 6, 6, fur);
  lcd.fillCircle(x + 24, y + 6, 6, fur);
  lcd.fillRoundRect(x, y + 8, 32, 24, 8, fur);
  lcd.fillCircle(x + 12, y + 18, 2, COL_BG);
  lcd.fillCircle(x + 22, y + 18, 2, COL_BG);
  lcd.fillCircle(x + 17, y + 24, 2, 0xFB2C);
}

static void paintRaceTrack() {
  lcd.fillRect(40, 188, 720, 100, pageBg());
  lcd.fillRoundRect(48, 236, 704, 16, 8, darkMode ? (uint16_t)0x3186 : (uint16_t)0xC618);
  lcd.fillCircle(736, 244, 20, 0x3B9F);
  drawTrackBridger(raceWalkerX(), 206);
}

static void paintRaceClock() {
  unsigned long left = RACE_MS - raceGone();
  int mins = (int)(left / 60000UL);
  int secs = (int)((left / 1000UL) % 60UL);
  char clock[8];
  snprintf(clock, sizeof(clock), "%d:%02d", mins, secs);
  lcd.fillRect(280, 118, 240, 52, pageBg());
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString(clock, 400, 144);
  lcd.setTextDatum(textdatum_t::top_left);
}

// Bridger talks only a few times, so the words do not keep changing.
static bool raceTalk() {
  unsigned long gone = raceGone();
  int mark = 0;
  if (gone >= RACE_MS) mark = 4;
  else if (gone >= 4UL * 60UL * 1000UL) mark = 3;
  else if (gone >= 150000UL) mark = 2;
  else if (gone >= 60000UL) mark = 1;
  if (mark == raceMark) {
    return false;
  }
  raceMark = mark;
  const char *line = "I'll walk. You can race me.";
  if (mark == 1) line = "Still going. No rush.";
  else if (mark == 2) line = "Halfway. I will wait.";
  else if (mark == 3) line = "Almost at the pond.";
  else if (mark == 4) line = "I waited. You did fine.";
  bridgerSay(line);
  return true;
}

// Five quiet minutes. The student can try to finish a job first.
// Bridger waits at the pond. Nobody loses.
static void paintRace() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Race with Bridger", 24, 64);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageSoft(), pageBg());
  lcd.drawString(bridgerHint, 24, 98);
  paintRaceClock();
  paintRaceTrack();
  if (!raceRunning) {
    lcd.fillRoundRect(24, 320, 752, 56, 12, softFill(COL_MINT));
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, softFill(COL_MINT));
    lcd.drawString(raceDone ? "Again" : "Start", 400, 348);
  }
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static void startRace() {
  raceRunning = true;
  raceDone = false;
  raceMs = millis();
  raceDrawMs = raceMs;
  raceMark = -1;
  raceTalk();
  paintRace();
}

// Approved accommodations. A tap asks the teacher.
static void paintAsk() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Ask for Help", 24, 70);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageSoft(), pageBg());
  lcd.drawString("Tell your teacher what you need", 24, 102);
  int aids = accOnCount();
  if (aids < 1) {
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(pageInk(), pageBg());
    lcd.drawString("No accommodations are turned on.", 400, 240);
  } else {
    int gap = 4;
    int height = (392 - 128 - (aids - 1) * gap) / aids;
    if (height > 48) {
      height = 48;
    }
    if (height < 28) {
      height = 28;
    }
    int placed = 0;
    for (int i = 0; i < ACC_COUNT; i++) {
      if (!accOn(i)) {
        continue;
      }
      int top = 128 + placed * (height + gap);
      lcd.fillRoundRect(24, top, 752, height, 12, softFill(COL_CREAM));
      lcd.setTextDatum(textdatum_t::middle_left);
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, softFill(COL_CREAM));
      lcd.drawString(ACCOMMODATIONS[i], 44, top + height / 2);
      placed++;
    }
  }
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static int askAtY(int y) {
  int aids = accOnCount();
  if (aids < 1) {
    return -1;
  }
  int gap = 4;
  int height = (392 - 128 - (aids - 1) * gap) / aids;
  if (height > 48) {
    height = 48;
  }
  if (height < 28) {
    height = 28;
  }
  int placed = 0;
  for (int i = 0; i < ACC_COUNT; i++) {
    if (!accOn(i)) {
      continue;
    }
    int top = 128 + placed * (height + gap);
    if (y >= top && y < top + height) {
      return i;
    }
    placed++;
  }
  return -1;
}

// Chalkboard or notebook. Shown only when Draw or write is on.
static void paintExpress() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Draw or Write", 24, 70);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageSoft(), pageBg());
  lcd.drawString("Express yourself another way", 24, 102);
  paintExpressClock();
  lcd.fillRoundRect(24, 150, 364, 220, 22, 0x13A4);
  lcd.drawRoundRect(24, 150, 364, 220, 22, 0xDEDB);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, 0x13A4);
  lcd.drawString("Chalkboard", 206, 250);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("Draw with chalk", 206, 286);
  paintHubCard(412, 150, 364, 220, COL_LILAC, "Notebook", "Type a few lines");
  drawPencil(594, 178);
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintFeedback() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  paintBrand(36);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Did that help?", 400, 96);
  const int xs[] = {24, 282, 540};
  const uint16_t fills[] = {softFill(COL_MINT), softFill(COL_PALE), softFill(COL_CREAM)};
  const char *labels[] = {"Yes", "Try again", "Not yet"};
  for (int i = 0; i < 3; i++) {
    lcd.fillRoundRect(xs[i], 132, 236, 156, 22, fills[i]);
    lcd.drawRoundRect(xs[i], 132, 236, 156, 22, 0xDEDB);
    if (i == 0) {
      drawCheck(xs[i] + 118, 184);
    } else if (i == 1) {
      drawRedo(xs[i] + 118, 184);
    } else {
      drawSoftX(xs[i] + 118, 184);
    }
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, fills[i]);
    lcd.drawString(labels[i], xs[i] + 118, 244);
  }
  char needLine[40];
  if (strcmp(teacherLabel(), "Teacher") == 0) {
    snprintf(needLine, sizeof(needLine), "I need my teacher");
  } else {
    snprintf(needLine, sizeof(needLine), "I need %s", teacherLabel());
  }
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString(needLine, 400, 340);
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static void openFeedback(uint8_t tool) {
  lastCope = tool;
  bridgerSay("Did that help?");
  currentScreen = SCREEN_FEED;
  paintFeedback();
}

// Three different looks, so each see tap is a new hunt.
static void pickSeeLooks() {
  int used = 0;
  while (used < 3) {
    int choice = (int)(esp_random() % SEE_LOOK_COUNT);
    bool already = false;
    for (int j = 0; j < used; j++) {
      if (seeLooks[j] == choice) {
        already = true;
      }
    }
    if (!already) {
      seeLooks[used] = choice;
      used++;
    }
  }
}

static void paintGround() {
  char line[32];
  char progress[12];
  const char *button = "I'm ready";
  const char *count = nullptr;
  progress[0] = 0;
  int step = groundStep;
  if (step < 0) {
    step = 0;
  }
  if (step > 6) {
    step = 6;
  }
  if (step < 3) {
    int look = seeLooks[step];
    if (look < 0 || look >= SEE_LOOK_COUNT) {
      look = 0;
    }
    snprintf(line, sizeof(line), "Find something %s", SEE_LOOKS[look]);
    snprintf(progress, sizeof(progress), "%d of 3", step + 1);
    button = "I see it";
    count = "3";
  } else if (step < 5) {
    snprintf(line, sizeof(line), "Find something to touch");
    snprintf(progress, sizeof(progress), "%d of 2", step - 2);
    button = "I touch it";
    count = "2";
  } else if (step < 6) {
    snprintf(line, sizeof(line), "Find a sound around you");
    button = "I hear it";
    count = "1";
  } else {
    snprintf(line, sizeof(line), "You found them");
  }
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(pageInk(), pageBg());
  if (count != nullptr) {
    lcd.setFont(&fonts::Font4);
    lcd.drawString(count, 400, 118);
  }
  lcd.setFont(&fonts::Font4);
  lcd.drawString(line, 400, 176);
  if (progress[0] != 0) {
    lcd.setFont(&fonts::Font2);
    lcd.drawString(progress, 400, 222);
  }
  lcd.fillRoundRect(GROUND_BTN_X, GROUND_BTN_Y, GROUND_BTN_W, GROUND_BTN_H, 12, COL_TRY);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_TRY);
  lcd.drawString(button, 400, GROUND_BTN_Y + GROUND_BTN_H / 2);
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintFirstThen() {
  lcd.fillScreen(pageBg());
  lcd.fillRoundRect(24, 72, 752, 120, 16, COL_ROW);
  lcd.fillRoundRect(24, 204, 752, 120, 16, COL_TRY);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.drawString("First", 48, 88);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(firstText, 48, 124);
  lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, COL_TRY);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("Then", 48, 220);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(thenText, 48, 256);
  lcd.fillRoundRect(24, 338, 752, 52, 12, COL_MINT);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, COL_MINT);
  lcd.drawString("I did it", 400, 364);
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static int scheduleBlockX(int index) {
  return SCHED_X0 + index * (SCHED_BLOCK_W + SCHED_ARROW);
}

static void paintSchedArrow(int blockX, int y, int h) {
  int x = blockX + SCHED_BLOCK_W + 6;
  int mid = y + h / 2;
  lcd.fillTriangle(x, mid - 14, x, mid + 14, x + 24, mid, COL_FG);
}

static void paintSchedBlock(int x, int y, int w, int h, const char *word, bool on) {
  uint16_t fill = on ? COL_TRY : COL_ROW;
  uint16_t ink = on ? (darkMode ? pageInk() : (uint16_t)COL_BG) : (uint16_t)COL_FG;
  lcd.fillRoundRect(x, y, w, h, 14, fill);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(ink, fill);
  lcd.setFont(&fonts::Font4);
  if (lcd.textWidth(word) > w - 16) {
    lcd.setFont(&fonts::Font2);
  }
  lcd.drawString(word, x + w / 2, y + h / 2);
}

static void paintSchedule() {
  if (scheduleNow < 0 || scheduleNow >= SCHED_COUNT) {
    scheduleNow = 0;
  }
  lcd.fillScreen(pageBg());
  for (int i = 0; i < SCHED_COUNT; i++) {
    int x = scheduleBlockX(i);
    paintSchedBlock(x, SCHED_SHOW_Y, SCHED_BLOCK_W, SCHED_SHOW_H, stepText[i], i == scheduleNow);
    if (i < SCHED_COUNT - 1) {
      paintSchedArrow(x, SCHED_SHOW_Y, SCHED_SHOW_H);
    }
  }
  lcd.fillRoundRect(SCHED_NEXT_X, SCHED_NEXT_Y, 480, SCHED_NEXT_H, 12, COL_TRY);
  lcd.fillRoundRect(520, SCHED_NEXT_Y, 256, SCHED_NEXT_H, 12, COL_MINT);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, COL_TRY);
  lcd.drawString("Next activity", SCHED_NEXT_X + 240, SCHED_NEXT_Y + SCHED_NEXT_H / 2);
  lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, COL_MINT);
  lcd.drawString("I did it", 648, SCHED_NEXT_Y + SCHED_NEXT_H / 2);
  paintBackButton();
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintFirstChoices();
static void paintScheduleChoices();
static void paintAccSettings();
static bool accOn(int index);

static void paintTools() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(pageInk(), pageBg());
  if (toolPage == 1) {
    paintFirstChoices();
    return;
  }
  if (toolPage == 2) {
    paintScheduleChoices();
    return;
  }
  if (toolPage == 4) {
    paintAccSettings();
    return;
  }
  if (toolPage == 5) {
    lcd.drawString("Strategy order", 24, 30);
    paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Back", COL_ROW);
    lcd.setFont(&fonts::Font2);
    for (int i = 0; i < 4; i++) {
      int top = 80 + i * 70;
      uint8_t bit = COPE_BITS[copeOrder[i]];
      const char *name = "Breathing";
      if (bit == TOOL_GROUND) name = "See, hear, feel";
      else if (bit == TOOL_FIRST) name = "First, then";
      else if (bit == TOOL_SCHED) name = "Schedule";
      lcd.fillRoundRect(24, top, 752, 58, 12, COL_ROW);
      lcd.setTextColor(COL_FG, COL_ROW);
      lcd.setTextDatum(textdatum_t::middle_left);
      lcd.drawString(name, 44, top + 29);
      if (i > 0) {
        lcd.fillRoundRect(620, top + 10, 140, 38, 8, COL_TRY);
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.setTextColor(darkMode ? pageInk() : (uint16_t)0x1082, COL_TRY);
        lcd.drawString("Up", 690, top + 29);
      }
    }
    lcd.setTextDatum(textdatum_t::top_left);
    return;
  }
  static const char *names[] = {
    "Zones", "See, hear, feel", "First, then", "Schedule", "Breathing"
  };
  static const uint8_t bits[] = {
    TOOL_ZONES, TOOL_GROUND, TOOL_FIRST, TOOL_SCHED, TOOL_BREATH
  };
  lcd.drawString("Tools for this student", 24, 30);
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Back", COL_ROW);
  for (int i = 0; i < 5; i++) {
    int top = 68 + i * 62;
    lcd.fillRoundRect(24, top, 752, 54, 12, COL_ROW);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, COL_ROW);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.drawString(names[i], 44, top + 27);
    bool on = toolOn(bits[i]);
    uint16_t fill = on ? COL_TRY : COL_FOOT;
    lcd.fillRoundRect(620, top + 8, 140, 38, 8, fill);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(COL_FG, fill);
    lcd.drawString(on ? "On" : "Off", 690, top + 27);
    if (i == 2 || i == 3) {
      lcd.fillRoundRect(470, top + 8, 136, 38, 8, COL_FOOT);
      lcd.setTextColor(COL_FG, COL_FOOT);
      lcd.drawString("Words", 538, top + 27);
    }
  }
  int accTop = 376;
  lcd.fillRoundRect(24, accTop, 752, 40, 10, COL_ROW);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.drawString("Accommodations", 44, accTop + 20);
  lcd.fillRoundRect(24, 424, 360, 44, 10, COL_ROW);
  lcd.fillRoundRect(400, 424, 376, 44, 10, COL_ROW);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.drawString("Strategy order", 40, 446);
  lcd.drawString("Calm screen", 416, 446);
  uint16_t calmFill = calmMode ? COL_TRY : COL_FOOT;
  lcd.fillRoundRect(620, 432, 140, 28, 8, calmFill);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(calmMode ? (darkMode ? pageInk() : (uint16_t)COL_BG) : (uint16_t)COL_FG, calmFill);
  lcd.drawString(calmMode ? "On" : "Off", 690, 446);
  lcd.setTextDatum(textdatum_t::top_left);
}

static void paintAccSettings() {
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Accommodations", 24, 30);
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Back", COL_ROW);
  for (int i = 0; i < ACC_COUNT; i++) {
    int top = 56 + i * 40;
    lcd.fillRoundRect(24, top, 752, 36, 8, COL_ROW);
    lcd.setTextColor(COL_FG, COL_ROW);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.drawString(ACCOMMODATIONS[i], 44, top + 18);
    bool on = accOn(i);
    uint16_t fill = on ? COL_TRY : COL_FOOT;
    lcd.fillRoundRect(620, top + 4, 140, 28, 6, fill);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setTextColor(on ? (darkMode ? pageInk() : (uint16_t)COL_BG) : (uint16_t)COL_FG, fill);
    lcd.drawString(on ? "On" : "Off", 690, top + 18);
  }
  lcd.setTextDatum(textdatum_t::top_left);
}

// Remember which screen is open, then draw it. The remember step
// has to happen first so a tap during the draw is handled correctly
// on the next loop. Home is the thermometer, not the old choice lists.
static void showMainMenu() {
  stopExpressTimer();
  openItem = nullptr;
  breathRunning = false;
  breathFromTools = false;
  if (toolOn(TOOL_ZONES)) {
    currentScreen = SCREEN_THERMO;
    paintThermometer();
    return;
  }
  currentScreen = SCREEN_ZONE;
  paintZone();
}

// Tell the teacher about one real choice. Opening a list does not
// call this. K may see a short word, but the note uses the full one.
static void notifyItem(const Item *item, const char *choiceText) {
  ClearNote note = {};
  note.version = CLEAR_LINK_VERSION;
  note.kind = CLEAR_KIND_NOTICE;
  note.asksTeacher = itemAsks(item) ? 1 : 0;
  note.answer = CLEAR_ANSWER_NONE;
  note.eventId = takeEventId();
  waitingId = note.eventId;
  teacherReply = CLEAR_ANSWER_NONE;
  noticeSent = false;
  sendFailed = false;
  note.timeSent = (uint32_t)time(nullptr);
  copyField(note.student, sizeof(note.student), studentName);
  copyField(note.feeling, sizeof(note.feeling), item->topic);
  copyField(note.choice, sizeof(note.choice), choiceText);
  rememberChoice(choiceText);
  outboundNote = note;
  needSend = true;
  nextBleMs = 0;
  saveClock();
}

// Yellow and red tell the teacher. Blue and green stay on the panel.
static void notifyZone() {
  const FeelingZone *zone = &feelingZones[thermoZone];
  ClearNote note = {};
  note.version = CLEAR_LINK_VERSION;
  note.kind = CLEAR_KIND_NOTICE;
  note.asksTeacher = 1;
  note.answer = CLEAR_ANSWER_NONE;
  note.eventId = takeEventId();
  waitingId = note.eventId;
  teacherReply = CLEAR_ANSWER_NONE;
  noticeSent = false;
  sendFailed = false;
  note.timeSent = (uint32_t)time(nullptr);
  copyField(note.student, sizeof(note.student), studentName);
  copyField(note.feeling, sizeof(note.feeling), zone->name);
  copyField(note.choice, sizeof(note.choice), "My energy is in this zone");
  rememberChoice("My energy is in this zone");
  outboundNote = note;
  needSend = true;
  nextBleMs = 0;
  saveClock();
}

static void commitZone() {
  bridgerSay("Let's pick a helper.");
  if (feelingZones[thermoZone].tellTeacher) {
    notifyZone();
  } else {
    teacherReply = CLEAR_ANSWER_NONE;
    waitingId = 0;
    noticeSent = false;
    sendFailed = false;
  }
  currentScreen = SCREEN_ZONE;
  paintZone();
}

static const char *copeChoice(uint8_t tool) {
  if (tool == TOOL_GROUND) return "See, hear, feel";
  if (tool == TOOL_FIRST) return "First, then";
  if (tool == TOOL_SCHED) return "Schedule";
  if (tool == TOOL_BREATH) return youngBreath() ? "Candle blowing" : "Box breathing";
  return "";
}

// Yes and Not yet are saved for the website. They do not ask the teacher
// to answer, and they do not change the Ready line. Blue and green stay
// on the panel, the same as opening the tool.
static void reportCope(bool helped) {
  if (!feelingZones[thermoZone].tellTeacher) {
    return;
  }
  const char *choice = copeChoice(lastCope);
  if (choice[0] == 0) {
    return;
  }
  ClearNote note = {};
  note.version = CLEAR_LINK_VERSION;
  note.kind = CLEAR_KIND_NOTICE;
  note.asksTeacher = helped ? CLEAR_ASK_HELPED : CLEAR_ASK_NOTYET;
  note.answer = CLEAR_ANSWER_NONE;
  note.eventId = takeEventId();
  note.timeSent = (uint32_t)time(nullptr);
  copyField(note.student, sizeof(note.student), studentName);
  copyField(note.feeling, sizeof(note.feeling), feelingZones[thermoZone].name);
  copyField(note.choice, sizeof(note.choice), choice);
  outboundNote = note;
  needSend = true;
  nextBleMs = 0;
  saveClock();
}

// Yellow, orange, and red tell the teacher which tool the student opened.
static void notifyTool(const char *choice) {
  if (!feelingZones[thermoZone].tellTeacher) {
    return;
  }
  ClearNote note = {};
  note.version = CLEAR_LINK_VERSION;
  note.kind = CLEAR_KIND_NOTICE;
  note.asksTeacher = 1;
  note.answer = CLEAR_ANSWER_NONE;
  note.eventId = takeEventId();
  waitingId = note.eventId;
  teacherReply = CLEAR_ANSWER_NONE;
  noticeSent = false;
  sendFailed = false;
  note.timeSent = (uint32_t)time(nullptr);
  copyField(note.student, sizeof(note.student), studentName);
  copyField(note.feeling, sizeof(note.feeling), feelingZones[thermoZone].name);
  copyField(note.choice, sizeof(note.choice), choice);
  rememberChoice(choice);
  outboundNote = note;
  needSend = true;
  nextBleMs = 0;
  saveClock();
}

static bool accOn(int index) {
  if (index < 0 || index >= ACC_COUNT) {
    return false;
  }
  return (accMask & (uint16_t)(1u << index)) != 0;
}

static int accOnCount() {
  int count = 0;
  for (int i = 0; i < ACC_COUNT; i++) {
    if (accOn(i)) {
      count++;
    }
  }
  return count;
}

static int accRowHeight() {
  int aids = accOnCount();
  if (aids < 1) {
    return 48;
  }
  int gap = 6;
  int height = (408 - 148 - (aids - 1) * gap) / aids;
  if (height > 48) {
    height = 48;
  }
  if (height < 32) {
    height = 32;
  }
  return height;
}

static int accAtY(int y) {
  int aids = accOnCount();
  if (aids < 1) {
    return -1;
  }
  int height = accRowHeight();
  int gap = 6;
  int placed = 0;
  for (int i = 0; i < ACC_COUNT; i++) {
    if (!accOn(i)) {
      continue;
    }
    int top = 148 + placed * (height + gap);
    if (y >= top && y < top + height) {
      return i;
    }
    placed++;
  }
  return -1;
}

static bool usesFooter() {
  return currentScreen != SCREEN_SETTINGS && currentScreen != SCREEN_NAME &&
         currentScreen != SCREEN_LOCK && currentScreen != SCREEN_TOOLS &&
         currentScreen != SCREEN_DRAW_MENU && currentScreen != SCREEN_DRAW_CANVAS;
}

static const char *instructionLine() {
  if (currentScreen == SCREEN_GROUND) {
    if (groundStep >= 5) return "Find a sound around you.";
    if (groundStep >= 3) return "Find something to touch.";
    return "Find something to see.";
  }
  if (currentScreen == SCREEN_SELECTED) {
    if (breathRunning) return "In, hold, out, hold.";
    if (breathOffer) return "Want another breath?";
    return "Press begin and breathe with me.";
  }
  if (currentScreen == SCREEN_FIRST) return "Do the first part, then the next.";
  if (currentScreen == SCREEN_SCHED) return "This block is now. Then the next.";
  if (currentScreen == SCREEN_ZONE) return "Pick what would help right now.";
  if (currentScreen == SCREEN_SOOTHE) return "Pick a calming tool.";
  if (currentScreen == SCREEN_PLAN) return "Pick a tool.";
  if (currentScreen == SCREEN_RACE) return "Bridger walks for five minutes. He will wait.";
  if (currentScreen == SCREEN_ASK) return "Tap what you need to ask.";
  if (currentScreen == SCREEN_EXPRESS) return "Draw on the board or write.";
  if (currentScreen == SCREEN_FEED) return "Did that help?";
  if (currentScreen == SCREEN_THERMO) return "Slide to your feeling, then tap This is me.";
  if (currentScreen == SCREEN_WRITE) return "Tap a line and type.";
  return "Hello. Show how you feel.";
}

static void paintRace();

static void replayInstruction() {
  bridgerSay(instructionLine());
  speakLine(instructionLine());
  if (currentScreen == SCREEN_RACE) {
    paintRace();
  } else if (usesFooter()) {
    paintStatus();
  }
}

static void sendHelp() {
  ClearNote note = {};
  note.version = CLEAR_LINK_VERSION;
  note.kind = CLEAR_KIND_NOTICE;
  note.asksTeacher = 1;
  note.answer = CLEAR_ANSWER_NONE;
  note.eventId = takeEventId();
  waitingId = note.eventId;
  teacherReply = CLEAR_ANSWER_NONE;
  noticeSent = false;
  sendFailed = false;
  note.timeSent = (uint32_t)time(nullptr);
  copyField(note.student, sizeof(note.student), studentName);
  copyField(note.feeling, sizeof(note.feeling), feelingZones[thermoZone].name);
  copyField(note.choice, sizeof(note.choice), "I need help");
  rememberChoice("I need help");
  bridgerSay("I told your teacher.");
  speakLine("I told your teacher.");
  outboundNote = note;
  needSend = true;
  nextBleMs = 0;
  saveClock();
  if (usesFooter()) {
    paintStatus();
  }
}

static uint16_t softFill(uint16_t color) {
  if (calmMode) {
    return darkMode ? 0x3186 : 0xC618;
  }
  return color;
}

// An accommodation always asks the teacher, on every zone.
static void askAccommodation(int index) {
  if (!accOn(index)) {
    return;
  }
  ClearNote note = {};
  note.version = CLEAR_LINK_VERSION;
  note.kind = CLEAR_KIND_NOTICE;
  note.asksTeacher = CLEAR_ASK_AID;
  note.answer = CLEAR_ANSWER_NONE;
  note.eventId = takeEventId();
  waitingId = note.eventId;
  teacherReply = CLEAR_ANSWER_NONE;
  noticeSent = false;
  sendFailed = false;
  note.timeSent = (uint32_t)time(nullptr);
  copyField(note.student, sizeof(note.student), studentName);
  copyField(note.feeling, sizeof(note.feeling), feelingZones[thermoZone].name);
  copyField(note.choice, sizeof(note.choice), ACCOMMODATIONS[index]);
  rememberChoice(ACCOMMODATIONS[index]);
  bridgerSay("I asked your teacher.");
  outboundNote = note;
  needSend = true;
  nextBleMs = 0;
  saveClock();
  paintAsk();
}

static int toolAtY(int y) {
  int shown = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t bit = COPE_BITS[copeOrder[i]];
    if (!toolOn(bit) || bit == TOOL_FIRST || bit == TOOL_SCHED) {
      continue;
    }
    int top = toolRowY(shown);
    if (y >= top && y < top + SOOTHE_ROW_H) {
      return bit;
    }
    shown++;
  }
  return 0;
}

static bool wordOnList(const char *word) {
  for (int i = 0; i < SCHOOL_ACTIVITY_COUNT; i++) {
    if (strcmp(word, SCHOOL_ACTIVITIES[i]) == 0) {
      return true;
    }
  }
  return false;
}

static int activityIndexAt(int x, int y) {
  for (int i = 0; i < SCHOOL_ACTIVITY_COUNT + 1; i++) {
    int col = i % ACT_COLS;
    int row = i / ACT_COLS;
    int left = ACT_X + col * ACT_STEP_X;
    int top = ACT_Y + row * ACT_STEP_Y;
    if (inBox(x, y, left, top, ACT_W, ACT_H)) {
      return i;
    }
  }
  return -1;
}

static void paintActivityBox(const char *sideWord) {
  lcd.fillRoundRect(ACT_BOX_X, ACT_BOX_Y, ACT_BOX_W, ACT_BOX_H, 16, COL_ROW);
  bool listed = wordOnList(sideWord);
  for (int i = 0; i < SCHOOL_ACTIVITY_COUNT + 1; i++) {
    int col = i % ACT_COLS;
    int row = i / ACT_COLS;
    int left = ACT_X + col * ACT_STEP_X;
    int top = ACT_Y + row * ACT_STEP_Y;
    bool custom = i == SCHOOL_ACTIVITY_COUNT;
    bool on = custom ? !listed : (strcmp(sideWord, SCHOOL_ACTIVITIES[i]) == 0);
    uint16_t fill = on ? COL_TRY : COL_BG;
    lcd.fillRoundRect(left, top, ACT_W, ACT_H, 8, fill);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(on ? (darkMode ? pageInk() : (uint16_t)COL_BG) : (uint16_t)COL_FG, fill);
    lcd.drawString(custom ? "Custom" : SCHOOL_ACTIVITIES[i], left + ACT_W / 2, top + ACT_H / 2);
  }
}

// Put the tapped activity on the green side. First then moves to Then.
static void setFirstSideWord(const char *word) {
  if (firstEditSide == 0) {
    copyField(firstText, sizeof(firstText), word);
    firstEditSide = 1;
  } else {
    copyField(thenText, sizeof(thenText), word);
  }
  saveSettings();
  paintTools();
}

static void paintHalf(int x, const char *label, const char *word, bool chosen) {
  uint16_t fill = chosen ? COL_TRY : COL_ROW;
  uint16_t ink = chosen ? (darkMode ? pageInk() : (uint16_t)COL_BG) : (uint16_t)COL_FG;
  lcd.fillRoundRect(x, HALF_Y, HALF_W, HALF_H, 16, fill);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(ink, fill);
  lcd.drawString(label, x + HALF_W / 2, HALF_Y + 36);
  lcd.setFont(&fonts::Font4);
  lcd.drawString(word, x + HALF_W / 2, HALF_Y + 92);
}

// First and Then sit side by side. The box under them is the
// activity list, with Custom in the last spot.
static void paintFirstChoices() {
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("First, then", 24, 30);
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Back", COL_ROW);
  paintHalf(HALF_LEFT, "First", firstText, firstEditSide == 0);
  paintHalf(HALF_RIGHT, "Then", thenText, firstEditSide == 1);
  paintActivityBox((firstEditSide == 0) ? firstText : thenText);
  lcd.setTextDatum(textdatum_t::top_left);
}

// The green block is the one an activity tap will fill.
static void paintScheduleChoices() {
  if (scheduleEdit < 0 || scheduleEdit >= SCHED_COUNT) {
    scheduleEdit = 0;
  }
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.drawString("Schedule", 24, 30);
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Back", COL_ROW);
  for (int i = 0; i < SCHED_COUNT; i++) {
    int x = scheduleBlockX(i);
    paintSchedBlock(x, SCHED_EDIT_Y, SCHED_BLOCK_W, SCHED_EDIT_H, stepText[i], i == scheduleEdit);
    if (i < SCHED_COUNT - 1) {
      paintSchedArrow(x, SCHED_EDIT_Y, SCHED_EDIT_H);
    }
  }
  paintActivityBox(stepText[scheduleEdit]);
  lcd.setTextDatum(textdatum_t::top_left);
}

static void setScheduleWord(const char *word) {
  if (scheduleEdit < 0 || scheduleEdit >= SCHED_COUNT) {
    scheduleEdit = 0;
  }
  copyField(stepText[scheduleEdit], sizeof(stepText[0]), word);
  if (scheduleEdit < SCHED_COUNT - 1) {
    scheduleEdit++;
  }
  saveSettings();
  paintTools();
}

static const char *toolFieldText() {
  if (toolField == 0) {
    return firstText;
  }
  if (toolField == 1) {
    return thenText;
  }
  if (toolField >= 2 && toolField <= 5) {
    return stepText[toolField - 2];
  }
  return firstText;
}

static void openToolEdit(int field) {
  toolField = field;
  typeMode = TYPE_TOOL;
  copyField(toolEdit, sizeof(toolEdit), toolFieldText());
  currentScreen = SCREEN_NAME;
  paintNameEditor();
}

static void saveToolEdit() {
  trimName(toolEdit);
  if (toolEdit[0] == 0) {
    paintNameEditor();
    return;
  }
  if (toolField == 0) {
    copyField(firstText, sizeof(firstText), toolEdit);
    firstEditSide = 1;
  } else if (toolField == 1) {
    copyField(thenText, sizeof(thenText), toolEdit);
  } else if (toolField >= 2 && toolField <= 5) {
    copyField(stepText[toolField - 2], sizeof(stepText[0]), toolEdit);
    scheduleEdit = toolField - 2;
    if (scheduleEdit < SCHED_COUNT - 1) {
      scheduleEdit++;
    }
  }
  saveSettings();
  currentScreen = SCREEN_TOOLS;
  paintTools();
}

static void openStudentTool(uint8_t bit) {
  lastCope = bit;
  if (bit == TOOL_GROUND) {
    bridgerSay("What do you see?");
    groundStep = 0;
    pickSeeLooks();
    notifyTool("See, hear, feel");
    currentScreen = SCREEN_GROUND;
    paintGround();
  } else if (bit == TOOL_FIRST) {
    bridgerSay("First, then a treat.");
    notifyTool("First, then");
    currentScreen = SCREEN_FIRST;
    paintFirstThen();
  } else if (bit == TOOL_SCHED) {
    bridgerSay("One block at a time.");
    notifyTool("Schedule");
    currentScreen = SCREEN_SCHED;
    paintSchedule();
  } else if (bit == TOOL_BREATH) {
    bridgerSay("Breathe with me.");
    notifyTool(youngBreath() ? "Candle blowing" : "Box breathing");
    breathFromTools = true;
    breathOffer = false;
    openItem = &distractedKids[0];
    breathRunning = false;
    currentScreen = SCREEN_SELECTED;
    paintSelected();
  }
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

static void startExpressTimer() {
  if (expressRunning) {
    return;
  }
  expressRunning = true;
  expressTimeUp = false;
  expressClockPaused = false;
  expressMs = millis();
  expressDrawMs = expressMs;
}

static void stopExpressTimer() {
  expressRunning = false;
  expressTimeUp = false;
  expressClockPaused = false;
}

// Five minutes is enough for a picture or a few lines. The panel
// then goes back to the help page. The drawing stays on the board.
static void endExpressTime() {
  finishDrawingStroke();
  if (currentScreen == SCREEN_NAME && typeMode == TYPE_WRITE) {
    trimName(messageEdit);
    copyField(writeLines[writeSlot], WRITE_MAX + 1, messageEdit);
  }
  if (currentScreen == SCREEN_DRAW_CANVAS &&
      (drawingSyncPending || drawingSyncWaitingForAck)) {
    expressTimeUp = true;
    exitAfterDrawingSync = true;
    setDrawStatus("Time is up. Finishing the save.");
    return;
  }
  expressRunning = false;
  expressTimeUp = false;
  fromExpress = false;
  bridgerSay("Five minutes. Time for a break.");
  currentScreen = SCREEN_ZONE;
  paintZone();
}

static void openWrite() {
  startExpressTimer();
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
  startExpressTimer();
  drawingStroke = false;
  lastDrawX = -1;
  lastDrawY = -1;
  cursorVisible = false;
  bool loaded = loadDrawingFile(board);
  loadDrawingOcr(board);
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
  clearDrawingOcr(selectedBoard);
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
  notifyItem(item, breathNoticeName(item));
  if (item->action == ACT_BREATHE) {
    bridgerSay("Breathe with me.");
  } else if (itemAsks(item)) {
    bridgerSay("I asked your teacher.");
  } else {
    bridgerSay("That can help.");
  }
  showDetail(item);
}

// Start the 4-second count. The notice was already sent when
// Box breathing was opened.
static void beginBreath() {
  bridgerSay("Nice and slow.");
  breathOffer = false;
  breathRunning = true;
  breathMs = millis();
  breathDrawMs = millis();
  paintSelected();
}

// Detail page and the writing page go back to the list.
// A nested list goes back to the list that opened it.
static void goBack() {
  breathRunning = false;
  breathOffer = false;
  if (breathFromTools && (currentScreen == SCREEN_SELECTED || currentScreen == SCREEN_GROUND ||
                           currentScreen == SCREEN_FIRST || currentScreen == SCREEN_SCHED)) {
    breathFromTools = false;
    currentScreen = SCREEN_SOOTHE;
    paintSoothe();
    return;
  }
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
  } else if (dest == SCREEN_ZONE) {
    currentScreen = SCREEN_ZONE;
    paintZone();
  } else if (dest == SCREEN_THERMO) {
    currentScreen = SCREEN_THERMO;
    paintThermometer();
  } else if (dest == SCREEN_GROUND) {
    currentScreen = SCREEN_GROUND;
    paintGround();
  } else if (dest == SCREEN_FIRST) {
    currentScreen = SCREEN_FIRST;
    paintFirstThen();
  } else if (dest == SCREEN_SCHED) {
    currentScreen = SCREEN_SCHED;
    paintSchedule();
  } else if (dest == SCREEN_SOOTHE) {
    currentScreen = SCREEN_SOOTHE;
    paintSoothe();
  } else if (dest == SCREEN_PLAN) {
    currentScreen = SCREEN_PLAN;
    paintPlan();
  } else if (dest == SCREEN_ASK) {
    currentScreen = SCREEN_ASK;
    paintAsk();
  } else if (dest == SCREEN_EXPRESS) {
    currentScreen = SCREEN_EXPRESS;
    paintExpress();
  } else if (dest == SCREEN_FEED) {
    currentScreen = SCREEN_FEED;
    paintFeedback();
  } else if (dest == SCREEN_TOOLS) {
    currentScreen = SCREEN_TOOLS;
    paintTools();
  } else {
    showMainMenu();
  }
}

// The gear asks for the settings password before name, grade, or tools.
// The first time, the teacher chooses that password and enters it twice.
static void openSettings() {
  returnScreen = currentScreen;
  pinEntry[0] = 0;
  if (!settingsPinSet) {
    lockJob = LOCK_SET_PASS;
    noteLock("Choose a password students do not know");
  } else {
    lockJob = LOCK_SETTINGS;
    noteLock("Enter the settings password");
  }
  currentScreen = SCREEN_LOCK;
  paintLock();
}

// Start the keyboard from the name that is already saved.
// Typing changes nameEdit only, until Save.
static void openNameEditor() {
  typeMode = TYPE_NAME;
  copyName(nameEdit, studentName);
  currentScreen = SCREEN_NAME;
  paintNameEditor();
}

static void openTeacherEditor() {
  typeMode = TYPE_TEACHER;
  copyName(nameEdit, teacherName);
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
  if (typeMode == TYPE_TOOL) {
    currentScreen = SCREEN_TOOLS;
    paintTools();
    return;
  }
  if (typeMode == TYPE_TEACHER) {
    currentScreen = SCREEN_SETTINGS;
    paintSettings();
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
  stopExpressTimer();
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

static void repaintCurrent() {
  if (currentScreen == SCREEN_HELLO) {
    paintHello();
  } else if (currentScreen == SCREEN_MENU) {
    paintList();
  } else if (currentScreen == SCREEN_SELECTED) {
    paintSelected();
  } else if (currentScreen == SCREEN_SETTINGS) {
    paintSettings();
  } else if (currentScreen == SCREEN_NAME) {
    paintNameEditor();
  } else if (currentScreen == SCREEN_WRITE) {
    paintWrite();
  } else if (currentScreen == SCREEN_DRAW_MENU) {
    paintDrawingMenu();
  } else if (currentScreen == SCREEN_DRAW_CANVAS) {
    paintDrawingCanvas();
  } else if (currentScreen == SCREEN_THERMO) {
    paintThermometer();
  } else if (currentScreen == SCREEN_ZONE) {
    paintZone();
  } else if (currentScreen == SCREEN_TOOLS) {
    paintTools();
  } else if (currentScreen == SCREEN_GROUND) {
    paintGround();
  } else if (currentScreen == SCREEN_FIRST) {
    paintFirstThen();
  } else if (currentScreen == SCREEN_SCHED) {
    paintSchedule();
  } else if (currentScreen == SCREEN_SOOTHE) {
    paintSoothe();
  } else if (currentScreen == SCREEN_ASK) {
    paintAsk();
  } else if (currentScreen == SCREEN_EXPRESS) {
    paintExpress();
  } else if (currentScreen == SCREEN_FEED) {
    paintFeedback();
  } else if (currentScreen == SCREEN_RACE) {
    paintRace();
  }
}

static void paintLock() {
  lcd.fillScreen(pageBg());
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(pageInk(), pageBg());
  lcd.setFont(&fonts::Font4);
  const char *title = "Class PIN";
  if (lockJob == LOCK_CHANGE) {
    title = "New class PIN";
  } else if (lockJob == LOCK_SET_PASS || lockJob == LOCK_SET_PASS_AGAIN || lockJob == LOCK_SETTINGS) {
    title = "Settings";
  }
  lcd.drawString(title, 24, 28);
  lcd.setFont(&fonts::Font2);
  lcd.drawString(lockNote, 24, 72);
  char dots[8] = "_ _ _ _";
  int filled = (int)strlen(pinEntry);
  if (filled > 4) {
    filled = 4;
  }
  for (int i = 0; i < filled; i++) {
    dots[i * 2] = '*';
  }
  lcd.setFont(&fonts::Font4);
  lcd.drawString(dots, 24, 108);
  static const char *labels[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "<"
  };
  const int padW = 150;
  const int padH = 64;
  const int gap = 12;
  const int left = (800 - (3 * padW + 2 * gap)) / 2;
  const int top = 168;
  for (int i = 0; i < 12; i++) {
    int x = left + (i % 3) * (padW + gap);
    int y = top + (i / 3) * (padH + gap);
    lcd.fillRoundRect(x, y, padW, padH, 12, COL_ROW);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(COL_FG, COL_ROW);
    lcd.drawString(labels[i], x + padW / 2, y + padH / 2);
  }
  if (lockJob == LOCK_SET_PASS || lockJob == LOCK_SET_PASS_AGAIN || lockJob == LOCK_SETTINGS) {
    paintBackButton();
  }
  lcd.setTextDatum(textdatum_t::top_left);
}

static void forgetBonds() {
#if defined(CONFIG_BLUEDROID_ENABLED)
  int count = esp_ble_get_bond_device_num();
  if (count > 0 && count < 16) {
    esp_ble_bond_dev_t *devs = new esp_ble_bond_dev_t[count];
    if (esp_ble_get_bond_device_list(&count, devs) == ESP_OK) {
      for (int i = 0; i < count; i++) {
        esp_ble_remove_bond_device(devs[i].bd_addr);
      }
    }
    delete[] devs;
  }
#endif
}

static void revealName() {
  if (sealedName.length() > 0) {
    String plain;
    if (clearOpenText(classPin, sealedName, plain)) {
      copyName(studentName, plain.c_str());
    }
  }
  if (studentName[0] == 0) {
    copyName(studentName, "Connor");
  }
  saveSettings();
}

static void finishUnlock() {
  revealName();
  startTeacherLink();
  activityMs = millis();
  pinEntry[0] = 0;
  if (privacyCover) {
    privacyCover = false;
    currentScreen = coveredScreen;
    repaintCurrent();
    return;
  }
  currentScreen = SCREEN_HELLO;
  screenMs = millis();
  paintHello();
}

static void noteLock(const char *text) {
  strncpy(lockNote, text, sizeof(lockNote) - 1);
  lockNote[sizeof(lockNote) - 1] = 0;
}

static bool settingsLockJob() {
  return lockJob == LOCK_SET_PASS || lockJob == LOCK_SET_PASS_AGAIN || lockJob == LOCK_SETTINGS;
}

// The class PIN opens the panel. This password opens settings only.
// A student who knows the class PIN still cannot change the tools.
static void acceptSettingsPin() {
  if (lockJob == LOCK_SET_PASS) {
    strncpy(settingsFirst, pinEntry, 4);
    settingsFirst[4] = 0;
    pinEntry[0] = 0;
    lockJob = LOCK_SET_PASS_AGAIN;
    noteLock("Enter that password again");
    paintLock();
    return;
  }
  if (lockJob == LOCK_SET_PASS_AGAIN) {
    if (strcmp(pinEntry, settingsFirst) != 0) {
      pinEntry[0] = 0;
      settingsFirst[0] = 0;
      lockJob = LOCK_SET_PASS;
      noteLock("Those did not match. Choose a password.");
      paintLock();
      return;
    }
    strncpy(settingsPin, pinEntry, 4);
    settingsPin[4] = 0;
    settingsPinSet = true;
    pinEntry[0] = 0;
    saveSettings();
    lockJob = LOCK_ENTER;
    currentScreen = SCREEN_SETTINGS;
    paintSettings();
    return;
  }
  if (strcmp(pinEntry, settingsPin) != 0) {
    pinEntry[0] = 0;
    noteLock("Try the settings password again");
    paintLock();
    return;
  }
  pinEntry[0] = 0;
  lockJob = LOCK_ENTER;
  currentScreen = SCREEN_SETTINGS;
  paintSettings();
}

static void acceptLockPin() {
  if (strlen(pinEntry) != 4) {
    return;
  }
  if (settingsLockJob()) {
    acceptSettingsPin();
    return;
  }
  if (lockJob == LOCK_CREATE || lockJob == LOCK_CHANGE) {
    for (int i = 0; i < 4; i++) {
      classPin[i] = pinEntry[i];
    }
    classPin[4] = 0;
    classPinSet = true;
    pinRejected = false;
    if (lockJob == LOCK_CHANGE) {
      if (teacherClient != nullptr && teacherClient->isConnected()) {
        teacherClient->disconnect();
      }
      forgetBonds();
      if (linkReady) {
        BLESecurity::setPassKey(true, (uint32_t)atoi(classPin));
      }
    }
    lockJob = LOCK_ENTER;
    privacyCover = false;
    finishUnlock();
    return;
  }
  if (clearTestPin(pinEntry)) {
    finishUnlock();
    return;
  }
  if (strcmp(pinEntry, classPin) != 0) {
    pinEntry[0] = 0;
    strncpy(lockNote, "Try the class PIN again", sizeof(lockNote) - 1);
    lockNote[sizeof(lockNote) - 1] = 0;
    paintLock();
    return;
  }
  strncpy(lockNote, "Ask your teacher for the class PIN", sizeof(lockNote) - 1);
  lockNote[sizeof(lockNote) - 1] = 0;
  finishUnlock();
}

static bool lockKeyAt(int x, int y, char *key) {
  static const char *labels[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "<"
  };
  const int padW = 150;
  const int padH = 64;
  const int gap = 12;
  const int left = (800 - (3 * padW + 2 * gap)) / 2;
  const int top = 168;
  for (int i = 0; i < 12; i++) {
    int keyX = left + (i % 3) * (padW + gap);
    int keyY = top + (i / 3) * (padH + gap);
    if (x >= keyX && x < keyX + padW && y >= keyY && y < keyY + padH) {
      *key = labels[i][0];
      return true;
    }
  }
  return false;
}

static void typeLockKey(char key) {
  int n = (int)strlen(pinEntry);
  if (key == 'C') {
    pinEntry[0] = 0;
  } else if (key == '<' && n > 0) {
    pinEntry[n - 1] = 0;
  } else if (key >= '0' && key <= '9' && n < 4) {
    pinEntry[n] = key;
    pinEntry[n + 1] = 0;
  }
  if (strlen(pinEntry) == 4) {
    acceptLockPin();
  } else {
    paintLock();
  }
}

static void coverPrivacy() {
  if (currentScreen == SCREEN_LOCK) {
    return;
  }
  coveredScreen = currentScreen;
  privacyCover = true;
  lockJob = LOCK_ENTER;
  pinEntry[0] = 0;
  strncpy(lockNote, "Enter the class PIN", sizeof(lockNote) - 1);
  lockNote[sizeof(lockNote) - 1] = 0;
  currentScreen = SCREEN_LOCK;
  paintLock();
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
  loadSettings();          // name, brightness, grade, and class PIN
  startClock();
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
  startAudio();
  drawStorageReady = SPIFFS.begin(false);
  currentDrawingId = newDrawingId();

  // The greeting stays hidden until the class PIN is entered.
  activityMs = millis();
  currentScreen = SCREEN_LOCK;
  if (lockJob == LOCK_CREATE) {
    strncpy(lockNote, "Ask your teacher for the class PIN", sizeof(lockNote) - 1);
  } else {
    strncpy(lockNote, "Enter the class PIN", sizeof(lockNote) - 1);
  }
  lockNote[sizeof(lockNote) - 1] = 0;
  paintLock();
  Serial.begin(115200);
  startPanelRadio();
}

// The website talks to this panel in short text lines. USB-C and
// Bluetooth use the same words. The settings password has to match
// before a line is accepted. The class PIN is a different password,
// and the test PIN does not open this door.
static const char *PANEL_SERVICE_UUID = "c1ea0001-1400-4c1e-8a11-000000000001";
static const char *PANEL_CHAR_UUID = "c1ea0002-1400-4c1e-8a11-000000000001";
static BLECharacteristic *panelChar = nullptr;
static char blePending[180];
static volatile bool blePendingReady = false;
static char usbLine[180];
static int usbLen = 0;
static bool usbAuthed = false;
static bool bleAuthed = false;
static bool settingsReplyUsb = true;
static int authFails = 0;
static unsigned long authLockUntil = 0;

class PanelWriteCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (blePendingReady) {
      return;
    }
    String value = characteristic->getValue();
    int n = value.length();
    if (n > (int)sizeof(blePending) - 1) {
      n = (int)sizeof(blePending) - 1;
    }
    for (int i = 0; i < n; i++) {
      char c = value.charAt(i);
      if (c == '\r' || c == '\n') {
        c = 0;
      }
      blePending[i] = c;
      if (c == 0) {
        n = i;
        break;
      }
    }
    blePending[n] = 0;
    blePendingReady = true;
  }
};

static void settingsSay(const char *line) {
  if (line == nullptr) {
    return;
  }
  if (settingsReplyUsb) {
    Serial.println(line);
    return;
  }
  if (panelChar != nullptr) {
    panelChar->setValue((uint8_t *)line, strlen(line));
    panelChar->notify();
    delay(12);
  }
}

static bool settingsLockedOut() {
  return authLockUntil != 0 && (long)(millis() - authLockUntil) < 0;
}

static void noteAuthFail() {
  authFails++;
  if (authFails >= 5) {
    authFails = 0;
    authLockUntil = millis() + 60000;
  }
}

static void copyPerson(char *dest, int maxLen, const char *src) {
  int n = 0;
  for (int i = 0; src[i] != 0 && n < maxLen; i++) {
    char c = src[i];
    bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    bool mark = (c == ' ' || c == '-' || c == '\'');
    if (!letter && !mark) {
      continue;
    }
    if (c == ' ' && n == 0) {
      continue;
    }
    dest[n++] = c;
  }
  dest[n] = 0;
}

static bool takeOrder(const char *text) {
  int vals[4] = {-1, -1, -1, -1};
  int n = 0;
  int cur = -1;
  for (int i = 0; text[i] != 0 && n < 4; i++) {
    if (text[i] >= '0' && text[i] <= '3') {
      cur = text[i] - '0';
    } else if (text[i] == ',') {
      if (cur < 0) {
        return false;
      }
      vals[n++] = cur;
      cur = -1;
    } else if (text[i] != ' ') {
      return false;
    }
  }
  if (cur >= 0 && n < 4) {
    vals[n++] = cur;
  }
  if (n != 4) {
    return false;
  }
  bool seen[4] = {false, false, false, false};
  for (int i = 0; i < 4; i++) {
    if (vals[i] < 0 || vals[i] > 3 || seen[vals[i]]) {
      return false;
    }
    seen[vals[i]] = true;
  }
  for (int i = 0; i < 4; i++) {
    copeOrder[i] = (uint8_t)vals[i];
  }
  return true;
}

static void applyRemoteSettings() {
  saveSettings();
  applyTheme();
  applyBrightness();
  if (currentScreen != SCREEN_NAME) {
    copyName(nameEdit, studentName);
  }
  if (currentScreen == SCREEN_LOCK || currentScreen == SCREEN_NAME) {
    return;
  }
  if (currentScreen == SCREEN_SETTINGS) {
    paintSettings();
    return;
  }
  int saved = returnScreen;
  returnScreen = currentScreen;
  showReturnScreen();
  returnScreen = saved;
}

static void sendSettingsList() {
  char line[96];
  snprintf(line, sizeof(line), "name=%s", studentName);
  settingsSay(line);
  snprintf(line, sizeof(line), "teacher=%s", teacherName);
  settingsSay(line);
  snprintf(line, sizeof(line), "bright=%d", brightnessPct);
  settingsSay(line);
  snprintf(line, sizeof(line), "grade=%d", gradeLevel);
  settingsSay(line);
  snprintf(line, sizeof(line), "dark=%d", darkMode ? 1 : 0);
  settingsSay(line);
  snprintf(line, sizeof(line), "bridger=%d", bridgerOn ? 1 : 0);
  settingsSay(line);
  snprintf(line, sizeof(line), "express=%d", expressOn ? 1 : 0);
  settingsSay(line);
  snprintf(line, sizeof(line), "calm=%d", calmMode ? 1 : 0);
  settingsSay(line);
  snprintf(line, sizeof(line), "tools=%u", (unsigned)toolMask);
  settingsSay(line);
  snprintf(line, sizeof(line), "acc=%u", (unsigned)accMask);
  settingsSay(line);
  snprintf(line, sizeof(line), "order=%u,%u,%u,%u",
           copeOrder[0], copeOrder[1], copeOrder[2], copeOrder[3]);
  settingsSay(line);
  snprintf(line, sizeof(line), "first=%s", firstText);
  settingsSay(line);
  snprintf(line, sizeof(line), "then=%s", thenText);
  settingsSay(line);
  snprintf(line, sizeof(line), "sch0=%s", stepText[0]);
  settingsSay(line);
  snprintf(line, sizeof(line), "sch1=%s", stepText[1]);
  settingsSay(line);
  snprintf(line, sizeof(line), "sch2=%s", stepText[2]);
  settingsSay(line);
  snprintf(line, sizeof(line), "sch3=%s", stepText[3]);
  settingsSay(line);
  settingsSay("END");
}

static bool applySetting(const char *key, const char *value) {
  if (strcmp(key, "name") == 0) {
    char next[STUDENT_NAME_MAX + 1];
    copyPerson(next, STUDENT_NAME_MAX, value);
    if (next[0] == 0) {
      return false;
    }
    copyName(studentName, next);
    return true;
  }
  if (strcmp(key, "teacher") == 0) {
    char next[17];
    copyPerson(next, 16, value);
    if (next[0] == 0) {
      return false;
    }
    copyName(teacherName, next);
    return true;
  }
  if (strcmp(key, "bright") == 0) {
    int pct = atoi(value);
    if (pct < BRIGHT_MIN || pct > BRIGHT_MAX) {
      return false;
    }
    brightnessPct = (pct / 10) * 10;
    return true;
  }
  if (strcmp(key, "grade") == 0) {
    int grade = atoi(value);
    if (grade < 0 || grade >= GRADE_COUNT) {
      return false;
    }
    gradeLevel = grade;
    return true;
  }
  if (strcmp(key, "dark") == 0 || strcmp(key, "bridger") == 0 ||
      strcmp(key, "express") == 0 || strcmp(key, "calm") == 0) {
    if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
      return false;
    }
    bool on = value[0] == '1';
    if (strcmp(key, "dark") == 0) darkMode = on;
    else if (strcmp(key, "bridger") == 0) bridgerOn = on;
    else if (strcmp(key, "express") == 0) expressOn = on;
    else calmMode = on;
    return true;
  }
  if (strcmp(key, "tools") == 0) {
    int mask = atoi(value);
    if (mask < 0 || mask > TOOL_ALL) {
      return false;
    }
    toolMask = (uint8_t)mask;
    return true;
  }
  if (strcmp(key, "acc") == 0) {
    int mask = atoi(value);
    int all = (1 << ACC_COUNT) - 1;
    if (mask < 0 || mask > all) {
      return false;
    }
    accMask = (uint16_t)mask;
    return true;
  }
  if (strcmp(key, "order") == 0) {
    return takeOrder(value);
  }
  if (strcmp(key, "first") == 0) {
    copyShort(firstText, sizeof(firstText), String(value), "Work");
    return firstText[0] != 0;
  }
  if (strcmp(key, "then") == 0) {
    copyShort(thenText, sizeof(thenText), String(value), "Break");
    return thenText[0] != 0;
  }
  if (strncmp(key, "sch", 3) == 0 && key[3] >= '0' && key[3] <= '3' && key[4] == 0) {
    int slot = key[3] - '0';
    copyShort(stepText[slot], sizeof(stepText[slot]), String(value), "Step");
    return stepText[slot][0] != 0;
  }
  if (strcmp(key, "pass") == 0) {
    if (strlen(value) != 4) {
      return false;
    }
    for (int i = 0; i < 4; i++) {
      if (value[i] < '0' || value[i] > '9') {
        return false;
      }
      settingsPin[i] = value[i];
    }
    settingsPin[4] = 0;
    settingsPinSet = true;
    return true;
  }
  return false;
}

static void handleSettingsLine(const char *line, bool fromUsb) {
  settingsReplyUsb = fromUsb;
  while (*line == ' ') {
    line++;
  }
  if (line[0] == 0) {
    return;
  }
  if (!settingsPinSet) {
    settingsSay("NO PIN");
    return;
  }
  if (settingsLockedOut()) {
    settingsSay("WAIT");
    return;
  }
  if (strncmp(line, "AUTH ", 5) == 0) {
    if (strcmp(line + 5, settingsPin) == 0) {
      if (fromUsb) usbAuthed = true;
      else bleAuthed = true;
      authFails = 0;
      authLockUntil = 0;
      settingsSay("OK");
    } else {
      if (fromUsb) usbAuthed = false;
      else bleAuthed = false;
      noteAuthFail();
      settingsSay("NO");
    }
    return;
  }
  bool authed = fromUsb ? usbAuthed : bleAuthed;
  if (!authed) {
    settingsSay("NO");
    return;
  }
  if (strcmp(line, "GET") == 0) {
    sendSettingsList();
    return;
  }
  if (strncmp(line, "SET ", 4) != 0) {
    settingsSay("NO");
    return;
  }
  const char *body = line + 4;
  const char *eq = strchr(body, '=');
  if (eq == nullptr || eq == body || eq[1] == 0) {
    settingsSay("NO");
    return;
  }
  char key[16];
  int keyLen = (int)(eq - body);
  if (keyLen <= 0 || keyLen >= (int)sizeof(key)) {
    settingsSay("NO");
    return;
  }
  memcpy(key, body, keyLen);
  key[keyLen] = 0;
  if (!applySetting(key, eq + 1)) {
    settingsSay("NO");
    return;
  }
  applyRemoteSettings();
  settingsSay("OK");
}

static void startPanelRadio() {
  ensureRadio();
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(PANEL_SERVICE_UUID);
  panelChar = service->createCharacteristic(
    PANEL_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );
  panelChar->addDescriptor(new BLE2902());
  panelChar->setCallbacks(new PanelWriteCallbacks());
  service->start();
  panelAd = BLEDevice::getAdvertising();
  panelAd->addServiceUUID(PANEL_SERVICE_UUID);
  panelAd->setScanResponse(true);
  panelAd->start();
}

static void servicePanelCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      usbLine[usbLen] = 0;
      usbLen = 0;
      handleSettingsLine(usbLine, true);
      continue;
    }
    if (usbLen < (int)sizeof(usbLine) - 1) {
      usbLine[usbLen++] = c;
    }
  }
  if (blePendingReady) {
    char copy[180];
    noInterrupts();
    strncpy(copy, blePending, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    blePendingReady = false;
    interrupts();
    handleSettingsLine(copy, false);
  }
}

// Repeats forever. A finger is read on every pass. The teacher search
// runs in the background so it does not hold the touch chip.
void loop() {
  servicePanelCommands();
  serviceOcrTextPacket();
  serviceAudio();

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
      sendFailed = false;
      if (note.timeSent > 1700000000UL) {
        time_t now = time(nullptr);
        if ((uint32_t)now + 30 < note.timeSent) {
          struct timeval tv;
          tv.tv_sec = note.timeSent;
          tv.tv_usec = 0;
          settimeofday(&tv, nullptr);
          saveClock();
          clockMs = millis();
        }
      }
      if (note.answer == CLEAR_ANSWER_YES) {
        bridgerSay("They said yes.");
      } else if (note.answer == CLEAR_ANSWER_NO) {
        bridgerSay("Not this time.");
      } else if (note.answer == CLEAR_ANSWER_LATER) {
        bridgerSay("In a minute.");
      } else if (note.answer == CLEAR_ANSWER_GOT) {
        bridgerSay("They have it.");
      } else if (note.answer == CLEAR_ANSWER_SEEN) {
        bridgerSay("They see it.");
      }
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

  // One press starts the candle or the box. It plays three full cycles,
  // then offers Again. Only the middle is redrawn while it runs.
  if (currentScreen == SCREEN_SELECTED && breathRunning &&
      (millis() - breathMs) >= BREATH_CYCLE_MS * BREATH_CYCLES) {
    breathRunning = false;
    if (breathFromTools) {
      openFeedback(TOOL_BREATH);
    } else {
      breathOffer = true;
      bridgerSay("Want another one?");
      paintBreathTool();
    }
  } else if (currentScreen == SCREEN_SELECTED && breathRunning && (millis() - breathDrawMs) >= 100) {
    breathDrawMs = millis();
    paintBreathMotion();
  }

  if (currentScreen == SCREEN_RACE && raceRunning) {
    if ((millis() - raceMs) >= RACE_MS) {
      raceRunning = false;
      raceDone = true;
      raceTalk();
      paintRace();
    } else if ((millis() - raceDrawMs) >= 1000) {
      raceDrawMs = millis();
      if (raceTalk()) {
        lcd.fillRect(24, 84, 700, 28, pageBg());
        lcd.setTextDatum(textdatum_t::middle_left);
        lcd.setFont(&fonts::Font2);
        lcd.setTextColor(pageSoft(), pageBg());
        lcd.drawString(bridgerHint, 24, 98);
      }
      paintRaceClock();
      paintRaceTrack();
    }
  }

  // The draw and write visit pauses on a lock or settings screen.
  // On the board itself it counts down, then leaves.
  if (expressRunning) {
    if (!expressCountsNow()) {
      if (!expressClockPaused) {
        expressClockPaused = true;
        expressPauseMs = millis();
      }
    } else {
      if (expressClockPaused) {
        expressMs += millis() - expressPauseMs;
        expressDrawMs = millis();
        expressClockPaused = false;
      }
      if (!expressTimeUp && (millis() - expressMs) >= EXPRESS_MS) {
        endExpressTime();
      } else if (!expressTimeUp && (millis() - expressDrawMs) >= 1000) {
        expressDrawMs = millis();
        paintExpressClock();
      }
    }
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
  if (currentScreen == SCREEN_DRAW_CANVAS && !drawingOcrVisible && !expressTimeUp) {
    if (!touchDown) {
      finishDrawingStroke();
    } else if (!drawingStroke && !touchWasDown &&
               ty >= DRAW_HEADER_H && ty < DRAW_CANVAS_BOTTOM) {
      int drawY = ty;
      int minDrawY = DRAW_HEADER_H + drawingInkWidth() / 2;
      if (drawY < minDrawY) {
        drawY = minDrawY;
      }
      clearDrawingOcr(selectedBoard);
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
  // Sliding the thermometer updates the zone while the finger moves.
  // This is me is left out so a drag does not leave the screen.
  if (currentScreen == SCREEN_THERMO && touchDown &&
      !inBox(tx, ty, GEAR_X, GEAR_Y, GEAR_W, GEAR_H) &&
      !inBox(tx, ty, THIS_X, THIS_Y, THIS_W, THIS_H) &&
      (fingerOnTube(tx, ty) || (tx >= LABEL_X && tx <= LABEL_X + LABEL_W && ty >= TUBE_TOP && ty <= TUBE_BOTTOM + BULB_R))) {
    int next = zoneFromY(ty);
    if (next != thermoZone) {
      thermoZone = next;
      bridgerSay(bridgerZoneLine());
      paintThermometer();
    }
  }
  if (touchDown && !touchWasDown && (millis() - tapMs) >= TAP_GAP_MS) {
    tapMs = millis();
    activityMs = millis();
    if (currentScreen == SCREEN_LOCK) {
      if (settingsLockJob() && inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        pinEntry[0] = 0;
        lockJob = LOCK_ENTER;
        showReturnScreen();
      } else {
        char key = 0;
        if (lockKeyAt(tx, ty, &key)) {
          typeLockKey(key);
        }
      }
    } else if (pinRejected && ty >= 420 &&
               (currentScreen == SCREEN_HELLO || currentScreen == SCREEN_MENU ||
                currentScreen == SCREEN_SELECTED || currentScreen == SCREEN_WRITE ||
                currentScreen == SCREEN_THERMO || currentScreen == SCREEN_ZONE ||
                currentScreen == SCREEN_GROUND || currentScreen == SCREEN_FIRST ||
                currentScreen == SCREEN_SCHED)) {
      lockJob = LOCK_CHANGE;
      privacyCover = false;
      pinEntry[0] = 0;
      strncpy(lockNote, "Enter the PIN from the teacher", sizeof(lockNote) - 1);
      lockNote[sizeof(lockNote) - 1] = 0;
      currentScreen = SCREEN_LOCK;
      paintLock();
    } else {
    bool onGearScreen = currentScreen == SCREEN_HELLO ||
                        currentScreen == SCREEN_MENU ||
                        currentScreen == SCREEN_SELECTED ||
                        currentScreen == SCREEN_THERMO ||
                        currentScreen == SCREEN_ZONE ||
                        currentScreen == SCREEN_SOOTHE ||
                        currentScreen == SCREEN_ASK ||
                        currentScreen == SCREEN_EXPRESS ||
                        currentScreen == SCREEN_FEED ||
                        currentScreen == SCREEN_GROUND ||
                        currentScreen == SCREEN_FIRST ||
                        currentScreen == SCREEN_SCHED ||
                        currentScreen == SCREEN_RACE ||
                        currentScreen == SCREEN_PLAN;
    // The gear is checked before the rest of the page. On the greeting,
    // any other tap skips ahead to the thermometer.
    if (usesFooter() && inBox(tx, ty, 8, 424, 96, 48)) {
      sendHelp();
    } else if (usesFooter() && inBox(tx, ty, 112, 424, 108, 48)) {
      replayInstruction();
    } else if (onGearScreen && inBox(tx, ty, GEAR_X, GEAR_Y, GEAR_W, GEAR_H)) {
      openSettings();
    } else if (currentScreen == SCREEN_HELLO) {
      showMainMenu();
    } else if (currentScreen == SCREEN_THERMO) {
      if (inBox(tx, ty, THIS_X, THIS_Y, THIS_W, THIS_H)) {
        commitZone();
      } else if (fingerOnTube(tx, ty) || (tx >= LABEL_X && tx <= LABEL_X + LABEL_W && ty >= TUBE_TOP && ty <= TUBE_BOTTOM + BULB_R)) {
        thermoZone = zoneFromY(ty);
        bridgerSay(bridgerZoneLine());
        paintThermometer();
      }
    } else if (currentScreen == SCREEN_ZONE) {
      if (toolOn(TOOL_ZONES) && inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        showMainMenu();
      } else if (inBox(tx, ty, 20, HUB_Y1, HUB_W, HUB_H)) {
        bridgerSay("Let's calm down.");
        currentScreen = SCREEN_SOOTHE;
        paintSoothe();
      } else if (inBox(tx, ty, 408, HUB_Y1, HUB_W, HUB_H)) {
        bridgerSay("I can ask for you.");
        currentScreen = SCREEN_ASK;
        paintAsk();
      } else if (expressOn && inBox(tx, ty, 20, HUB_Y2, HUB_W, HUB_H)) {
        bridgerSay("Draw or write.");
        currentScreen = SCREEN_EXPRESS;
        paintExpress();
      } else if (inBox(tx, ty, expressOn ? 408 : 20, HUB_Y2, expressOn ? HUB_W : 760, HUB_H)) {
        bridgerSay("Pick a tool.");
        currentScreen = SCREEN_PLAN;
        paintPlan();
      }
    } else if (currentScreen == SCREEN_SOOTHE) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        currentScreen = SCREEN_ZONE;
        paintZone();
      } else {
        int bit = toolAtY(ty);
        if (bit != 0) {
          openStudentTool((uint8_t)bit);
        }
      }
    } else if (currentScreen == SCREEN_PLAN) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        currentScreen = SCREEN_ZONE;
        paintZone();
      } else {
        int shown = 0;
        int picked = 0;
        for (int i = 0; i < 4; i++) {
          uint8_t bit = COPE_BITS[copeOrder[i]];
          if (bit != TOOL_FIRST && bit != TOOL_SCHED) {
            continue;
          }
          if (!toolOn(bit)) {
            continue;
          }
          int top = toolRowY(shown);
          if (inBox(tx, ty, 24, top, 752, SOOTHE_ROW_H)) {
            picked = bit;
          }
          shown++;
        }
        int raceTop = toolRowY(shown);
        if (picked != 0) {
          openStudentTool((uint8_t)picked);
        } else if (inBox(tx, ty, 24, raceTop, 752, SOOTHE_ROW_H)) {
          raceRunning = false;
          raceDone = false;
          raceMark = -1;
          bridgerSay("I'll walk. You can race me.");
          currentScreen = SCREEN_RACE;
          paintRace();
        }
      }
    } else if (currentScreen == SCREEN_RACE) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        raceRunning = false;
        currentScreen = SCREEN_PLAN;
        paintPlan();
      } else if (!raceRunning && inBox(tx, ty, 24, 320, 752, 56)) {
        startRace();
      }
    } else if (currentScreen == SCREEN_ASK) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        currentScreen = SCREEN_ZONE;
        paintZone();
      } else {
        int aid = askAtY(ty);
        if (aid >= 0) {
          askAccommodation(aid);
        }
      }
    } else if (currentScreen == SCREEN_EXPRESS) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        stopExpressTimer();
        currentScreen = SCREEN_ZONE;
        paintZone();
      } else if (inBox(tx, ty, 24, 150, 364, 220)) {
        fromExpress = true;
        bridgerSay("Make a picture.");
        openDrawingCanvas(BOARD_CHALK);
      } else if (inBox(tx, ty, 412, 150, 364, 220)) {
        fromExpress = true;
        bridgerSay("Write a little.");
        openItem = nullptr;
        openWrite();
      }
    } else if (currentScreen == SCREEN_FEED) {
      if (inBox(tx, ty, 24, 132, 236, 156)) {
        reportCope(true);
        bridgerSay("Glad that helped.");
        currentScreen = SCREEN_ZONE;
        paintZone();
      } else if (inBox(tx, ty, 282, 132, 236, 156)) {
        if (lastCope != 0) {
          openStudentTool(lastCope);
        } else {
          currentScreen = SCREEN_SOOTHE;
          paintSoothe();
        }
      } else if (inBox(tx, ty, 540, 132, 236, 156)) {
        reportCope(false);
        bridgerSay("We can try another.");
        if (lastCope == TOOL_FIRST || lastCope == TOOL_SCHED) {
          currentScreen = SCREEN_PLAN;
          paintPlan();
        } else {
          currentScreen = SCREEN_SOOTHE;
          paintSoothe();
        }
      } else if (inBox(tx, ty, 160, 312, 480, 48)) {
        ClearNote note = {};
        note.version = CLEAR_LINK_VERSION;
        note.kind = CLEAR_KIND_NOTICE;
        note.asksTeacher = 1;
        note.answer = CLEAR_ANSWER_NONE;
        note.eventId = takeEventId();
        waitingId = note.eventId;
        teacherReply = CLEAR_ANSWER_NONE;
        noticeSent = false;
        sendFailed = false;
        note.timeSent = (uint32_t)time(nullptr);
        copyField(note.student, sizeof(note.student), studentName);
        copyField(note.feeling, sizeof(note.feeling), feelingZones[thermoZone].name);
        copyField(note.choice, sizeof(note.choice), "I need my teacher");
        rememberChoice("I need my teacher");
        bridgerSay("I asked your teacher.");
        outboundNote = note;
        needSend = true;
        nextBleMs = 0;
        saveClock();
        paintFeedback();
      }
    } else if (currentScreen == SCREEN_GROUND) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        currentScreen = SCREEN_SOOTHE;
        paintSoothe();
      } else if (inBox(tx, ty, GROUND_BTN_X, GROUND_BTN_Y, GROUND_BTN_W, GROUND_BTN_H)) {
        if (groundStep >= 6) {
          openFeedback(TOOL_GROUND);
        } else {
          groundStep++;
          if (groundStep >= 6) {
            openFeedback(TOOL_GROUND);
          } else if (groundStep >= 5) {
            bridgerSay("What do you hear?");
            paintGround();
          } else if (groundStep >= 3) {
            bridgerSay("What can you touch?");
            paintGround();
          } else {
            bridgerSay("What do you see?");
            paintGround();
          }
        }
      }
    } else if (currentScreen == SCREEN_FIRST) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        currentScreen = SCREEN_PLAN;
        paintPlan();
      } else if (inBox(tx, ty, 24, 338, 752, 52)) {
        openFeedback(TOOL_FIRST);
      }
    } else if (currentScreen == SCREEN_SCHED) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        currentScreen = SCREEN_PLAN;
        paintPlan();
      } else if (inBox(tx, ty, 520, SCHED_NEXT_Y, 256, SCHED_NEXT_H)) {
        openFeedback(TOOL_SCHED);
      } else if (inBox(tx, ty, SCHED_NEXT_X, SCHED_NEXT_Y, 480, SCHED_NEXT_H)) {
        scheduleNow++;
        if (scheduleNow >= SCHED_COUNT) {
          scheduleNow = 0;
        }
        bridgerSay("On to the next one.");
        paintSchedule();
      }
    } else if (currentScreen == SCREEN_TOOLS) {
      if (inBox(tx, ty, DONE_X, DONE_Y, DONE_W, DONE_H)) {
        if (toolPage == 0) {
          currentScreen = SCREEN_SETTINGS;
          paintSettings();
        } else {
          toolPage = 0;
          paintTools();
        }
      } else if (toolPage == 1) {
        if (inBox(tx, ty, HALF_LEFT, HALF_Y, HALF_W, HALF_H)) {
          firstEditSide = 0;
          paintTools();
        } else if (inBox(tx, ty, HALF_RIGHT, HALF_Y, HALF_W, HALF_H)) {
          firstEditSide = 1;
          paintTools();
        } else {
          int picked = activityIndexAt(tx, ty);
          if (picked >= 0 && picked < SCHOOL_ACTIVITY_COUNT) {
            setFirstSideWord(SCHOOL_ACTIVITIES[picked]);
          } else if (picked == SCHOOL_ACTIVITY_COUNT) {
            openToolEdit(firstEditSide);
          }
        }
      } else if (toolPage == 2) {
        bool onBlock = false;
        for (int i = 0; i < SCHED_COUNT; i++) {
          if (inBox(tx, ty, scheduleBlockX(i), SCHED_EDIT_Y, SCHED_BLOCK_W, SCHED_EDIT_H)) {
            scheduleEdit = i;
            paintTools();
            onBlock = true;
            break;
          }
        }
        if (!onBlock) {
          int picked = activityIndexAt(tx, ty);
          if (picked >= 0 && picked < SCHOOL_ACTIVITY_COUNT) {
            setScheduleWord(SCHOOL_ACTIVITIES[picked]);
          } else if (picked == SCHOOL_ACTIVITY_COUNT) {
            openToolEdit(2 + scheduleEdit);
          }
        }
      } else if (toolPage == 5) {
        for (int i = 1; i < 4; i++) {
          int top = 80 + i * 70;
          if (inBox(tx, ty, 620, top + 10, 140, 38)) {
            uint8_t swap = copeOrder[i - 1];
            copeOrder[i - 1] = copeOrder[i];
            copeOrder[i] = swap;
            saveSettings();
            paintTools();
            break;
          }
        }
      } else if (toolPage == 4) {
        for (int i = 0; i < ACC_COUNT; i++) {
          int top = 56 + i * 40;
          if (inBox(tx, ty, 24, top, 752, 36)) {
            accMask = (uint16_t)(accMask ^ (uint16_t)(1u << i));
            saveSettings();
            paintTools();
            break;
          }
        }
      } else if (inBox(tx, ty, 24, 424, 360, 44)) {
        toolPage = 5;
        paintTools();
      } else if (inBox(tx, ty, 400, 424, 376, 44)) {
        calmMode = !calmMode;
        saveSettings();
        paintTools();
      } else if (inBox(tx, ty, 24, 376, 752, 40)) {
        toolPage = 4;
        paintTools();
      } else if (tx >= 470 && tx < 606) {
        int row = (ty - 68) / 62;
        if (row == 2) {
          toolPage = 1;
          firstEditSide = 0;
          paintTools();
        } else if (row == 3) {
          toolPage = 2;
          scheduleEdit = 0;
          paintTools();
        }
      } else {
        static const uint8_t bits[] = {
          TOOL_ZONES, TOOL_GROUND, TOOL_FIRST, TOOL_SCHED, TOOL_BREATH
        };
        for (int i = 0; i < 5; i++) {
          int top = 68 + i * 62;
          if (inBox(tx, ty, 24, top, 752, 54)) {
            toolMask = (uint8_t)(toolMask ^ bits[i]);
            saveSettings();
            paintTools();
            break;
          }
        }
      }
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
    } else if (currentScreen == SCREEN_DRAW_CANVAS && !expressTimeUp) {
      if (inBox(tx, ty, DRAW_BACK_X, DRAW_BACK_Y, DRAW_BACK_W, DRAW_BACK_H)) {
        if (drawingSegmentCount == 0) {
          if (fromExpress) {
            fromExpress = false;
            currentScreen = SCREEN_EXPRESS;
            paintExpress();
          } else {
            openDrawingMenu();
          }
        } else if (drawingSyncPending || drawingSyncWaitingForAck) {
          exitAfterDrawingSync = true;
          setDrawStatus("Finishing transfer before leaving the board");
        } else if (prepareDrawingSync(false)) {
          exitAfterDrawingSync = true;
          setDrawStatus("Sending drawing before leaving the board");
        }
      } else if (inBox(tx, ty, DRAW_READ_X, DRAW_BUTTON_Y, 88, DRAW_BUTTON_H)) {
        if (drawingOcrVisible) {
          drawingOcrVisible = false;
          paintDrawingCanvas();
        } else if (loadDrawingOcr(selectedBoard)) {
          drawingOcrVisible = true;
          paintDrawingCanvas();
        } else {
          setDrawStatus("Readable text is not available for this board yet");
        }
      } else if (drawingOcrVisible && ty >= DRAW_CANVAS_BOTTOM) {
        drawingOcrPage = (drawingOcrPage + 1) % drawingOcrPageCount;
        paintDrawingCanvas();
      } else if (selectedBoard == BOARD_CHALK &&
                 inBox(tx, ty, DRAW_COLOR_X[0], DRAW_COLOR_Y,
                       DRAW_COLOR_X[DRAW_COLOR_COUNT - 1] +
                         DRAW_COLOR_W - DRAW_COLOR_X[0], DRAW_COLOR_H)) {
        for (int i = 0; i < DRAW_COLOR_COUNT; i++) {
          if (inBox(tx, ty, DRAW_COLOR_X[i], DRAW_COLOR_Y,
                    DRAW_COLOR_W, DRAW_COLOR_H)) {
            selectedChalkColor = i;
            saveDrawingFile();
            paintChalkChoices();
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
      if (inBox(tx, ty, NAME_BACK_X, NAME_BACK_Y, NAME_BACK_W, NAME_BACK_H)) {
        if (fromExpress) {
          fromExpress = false;
          currentScreen = SCREEN_EXPRESS;
          paintExpress();
        } else {
          stopExpressTimer();
          goBack();
        }
      } else if (inBox(tx, ty, DONE_X, DONE_Y, DONE_W, DONE_H)) {
        if (fromExpress) {
          stopExpressTimer();
          fromExpress = false;
          bridgerSay("Nice writing.");
          currentScreen = SCREEN_ZONE;
          paintZone();
        } else {
          finishWriting();
        }
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
      } else if (inBox(tx, ty, ROW_X, BEAVER_Y, 368, BEAVER_H)) {
        bridgerOn = !bridgerOn;
        saveSettings();
        paintSettings();
      } else if (inBox(tx, ty, 408, BEAVER_Y, 368, BEAVER_H)) {
        expressOn = !expressOn;
        saveSettings();
        paintSettings();
      } else if (inBox(tx, ty, 360, 8, 168, 44)) {
        toolPage = 0;
        currentScreen = SCREEN_TOOLS;
        paintTools();
      } else if (inBox(tx, ty, ROW_X, TEACH_Y, ROW_W, TEACH_H)) {
        openTeacherEditor();
      } else if (inBox(tx, ty, 500, LOOK_Y, 120, LOOK_H)) {
        darkMode = false;
        applyTheme();
        saveSettings();
        paintSettings();
      } else if (inBox(tx, ty, 632, LOOK_Y, 120, LOOK_H)) {
        darkMode = true;
        applyTheme();
        saveSettings();
        paintSettings();
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
        } else if (typeMode == TYPE_TOOL) {
          saveToolEdit();
        } else if (typeMode == TYPE_TEACHER) {
          trimName(nameEdit);
          if (nameEdit[0] == 0) {
            copyName(nameEdit, "Teacher");
          }
          copyName(teacherName, nameEdit);
          saveSettings();
          closeNameEditor();
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
  }
  if (touchDown) {
    activityMs = millis();
  } else if (!breathRunning && !raceRunning && classPinSet && currentScreen != SCREEN_LOCK &&
             (millis() - activityMs) >= PRIVACY_IDLE_MS) {
    coverPrivacy();
  }
  // Remember this pass so the next one can tell a new press from a held finger.
  // The teacher search runs only after the finger is up, so a tap is
  // not thrown away while the radio is busy.
  touchWasDown = touchDown;
  if (!touchDown) {
    blePump();
  }
}