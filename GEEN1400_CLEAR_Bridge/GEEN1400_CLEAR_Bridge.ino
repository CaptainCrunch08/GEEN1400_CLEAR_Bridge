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

  RemoteXY opens a Wi-Fi hotspot on the ESP32-S3. The HM-10 stays unplugged.
  Join the Wi-Fi network RemoteXY, password 12345678, then open the app.

  Green buttons are something the student can try.
  Orange buttons ask the teacher, for approval on the phone later.

  The gear in the top right opens settings. A teacher can change the
  name on the startup screen, the brightness, and the grade-level look.
  Grades 2-4 is the screen students see. The other grades are placeholders.

  How this sketch runs:
  setup() runs once at power-on. loop() then repeats forever.
  There is no web page on the panel. Each paint function draws one
  whole screen. A finger tap is noticed in loop(), and that changes
  which screen variable is current. The next paint draws that screen.
  The name, brightness, and grade are written to flash, so they are
  still there after the panel restarts.
*/

// Tell RemoteXY to be its own Wi-Fi network, not to join the school Wi-Fi.
// The phone joins that network and opens the RemoteXY app.
#define REMOTEXY_MODE__WIFI_POINT

#include <WiFi.h>

// Name and password of the hotspot the panel creates. 6377 is the port
// the RemoteXY phone app expects. These must match the app.
#define REMOTEXY_WIFI_SSID "RemoteXY"
#define REMOTEXY_WIFI_PASSWORD "12345678"
#define REMOTEXY_SERVER_PORT 6377

#include <RemoteXY.h>
#include <Wire.h>          // I2C bus, used by the touch chip and the small expander
#include <Preferences.h>   // saves a few settings in flash across restarts
#include <LovyanGFX.hpp>   // draws text, rectangles, and reads the touch panel
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// The bytes below were exported from the RemoteXY editor. They describe
// the phone screen. Edit that screen in the editor, then paste a new
// export here. Do not change individual numbers by hand.
#pragma pack(push, 1)
uint8_t const PROGMEM RemoteXY_CONF_PROGMEM[] = {  // 129 bytes V19
  255,2,0,3,0,122,0,19,0,0,0,0,31,1,106,200,1,1,5,0,
  3,12,96,26,71,3,2,26,12,10,8,48,10,193,30,26,83,116,117,100,
  101,110,116,32,49,0,83,116,117,100,101,110,116,32,50,0,83,116,117,100,
  101,110,116,32,51,0,74,49,102,46,10,12,2,30,37,64,82,101,115,112,
  111,110,115,101,32,49,0,74,49,127,45,10,12,2,30,37,64,82,101,115,
  112,111,110,115,101,32,50,0,74,49,151,45,10,12,2,30,37,64,82,101,
  115,112,111,110,115,101,32,51,0
};

// Live values from the phone. The library fills these in.
// loop() only reads them. The packing lines keep the byte order RemoteXY needs.
struct {
  uint8_t selectorSwitch_01; // which student the phone picked: 0 none, 1, 2, or 3
  uint8_t optionSelector_01; // which response the phone picked: 0 none, 1, 2, or 3
  uint8_t strings_01; // from 0 to 1, sent back to the phone; this sketch leaves it alone
  uint8_t strings_02; // from 0 to 1
  uint8_t strings_03; // from 0 to 1
  uint8_t connect_flag;  // 1 when the phone app is connected, otherwise 0
} RemoteXY;
#pragma pack(pop)

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
      cfg.freq = 400000;
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
static const int SCREEN_HELLO = 0;      // "Hi, name"
static const int SCREEN_MENU = 1;       // the five feelings
static const int SCREEN_SUBMENU = 2;    // responses for the feeling they picked
static const int SCREEN_SELECTED = 3;   // the steps for one response
static const int SCREEN_SETTINGS = 4;   // name, brightness, grade
static const int SCREEN_NAME = 5;       // keyboard for the student name

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
// NAME_MAX is the longest name that fits in the saved setting and
// on the greeting line.
static const int NAME_MAX = 16;
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
// Index into gradeOptions of the layout students actually see today.
static const int GRADE_LIVE = 2;

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
//   screen colors, grade-level rows, the five feelings, and every response.
//   Change the text in quotes to change what a student reads.
//   true  = orange "Ask teacher" button
//   false = green "Try this" button
//   If you add or remove a response, also change the matching count below
//   (anxietyCount, boredomCount, and the others).
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

// One row in the grade list. live is true only for the layout that
// is really on the student screens. The others are saved choices for later.
struct GradeOption {
  const char *label;
  const char *about;
  bool live;
};

// Stored for a later screen swap. Only Grades 2-4 is drawn for students today.
static const GradeOption gradeOptions[] = {
  {"Kindergarten", "Pictures and very few words", false},
  {"Grade 1", "Big letters and few buttons", false},
  {"Grades 2-4", "Short sentences, this screen", true},
  {"Grade 5", "More reading, smaller type", false}
};

// One response under a feeling.
// label is the button text. help is the longer instruction on the next screen.
// asksTeacher true means the button is orange and needs a teacher.
// false means the student can try it alone, and the button is green.
// A \n inside help starts a new line on the detail screen.
struct Choice {
  const char *label;
  const char *help;
  bool asksTeacher;
};

// The five feelings, in the same order as the lists below.
// selectedMenu is an index into this array: 0 is Worried, 4 is Uncomfortable.
static const char *menu[] = {
  "Worried",
  "Bored",
  "I don't get it",
  "Mad",
  "Uncomfortable"
};
// Responses for each feeling, in menu[] order.
// Worried is index 0. The last true/false on each line is asksTeacher.
static const Choice anxietyMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"I need a walk", "Ask your teacher if you can take a short walk.", true},
  {"Help me calm down", "Sit down.\nPut your feet on the floor.\nOpen your hands.\nBreathe in, then breathe out.\nDo this 5 times.", false},
  {"Talk to someone", "Ask your teacher if you can talk with someone.", true},
  {"Breathing", "Breathe in while you count to 4.\nHold for 2.\nBreathe out while you count to 4.\nDo this 5 times.", false},
  {"Talk after class", "Ask to meet your teacher after class.", true}
};
// Boredom. menu index 1.
static const Choice boredomMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"Draw how I feel", "Draw a picture of how you feel. You can show it when you are ready.", false},
  {"Quiet looking game", "Find 5 blue things.\nThen find 4 round things.\nThen find 3 soft things.", false}
};
// "I don't get it". menu index 2.
static const Choice understandingMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"Draw the problem", "Draw the problem.\nCircle the part you do not understand.", false},
  {"Please say that again", "Ask your teacher to explain the last part again.", true},
  {"Explain this question", "Ask your teacher to explain this question.", true},
  {"Can we practice one?", "Ask for one example or practice problem.", true}
};
// Mad. menu index 3.
static const Choice angerMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"I need a walk", "Ask your teacher if you can take a short walk.", true},
  {"Breathing", "Breathe in while you count to 4.\nHold for 2.\nBreathe out while you count to 4.\nDo this 5 times.", false},
  {"Talk to someone", "Ask your teacher if you can talk with someone.", true},
  {"Help me cool down", "Stop.\nPut your hands in your lap.\nCount slowly to 10.\nTake one slow breath.", false}
};
// Uncomfortable. menu index 4.
static const Choice uncomfortableMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"I need a walk", "Ask your teacher if you can take a short walk.", true},
  {"Please help me", "Ask your teacher for help with this problem.", true},
  {"Can I see an example?", "Ask your teacher to show you one example.", true}
};

// How many buttons each list has. These must match the arrays above.
// They are separate numbers because a C array of structs does not
// carry its length around with it.
static const int totalLines = 5;
static const int anxietyCount = 6;
static const int boredomCount = 3;
static const int understandingCount = 5;
static const int angerCount = 5;
static const int uncomfortableCount = 4;

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
static int selectedMenu = 0;          // which feeling was tapped
static int submenuPos = 0;            // which response inside that feeling
static unsigned long screenMs = 0;    // when the greeting was shown
static unsigned long tapMs = 0;       // when the last accepted tap happened
static bool touchWasDown = false;     // so one press counts once, not every loop
static bool nameDirty = false;        // name changed, so Done should show the greeting
static uint8_t shownConnect = 0xFF;   // last phone values drawn in the footer
static uint8_t shownSwitch = 0xFF;    // 0xFF means "not drawn yet", so the first
static uint8_t shownOption = 0xFF;    // real value always gets painted
static char studentName[NAME_MAX + 1] = "Connor"; // name on the greeting
static char nameEdit[NAME_MAX + 1] = "Connor";    // name being typed, not saved yet
static int brightnessPct = 80;
static int gradeLevel = GRADE_LIVE;

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
// which is how C marks the end of a string. Stops at NAME_MAX
// so a long saved value cannot write past the array.
static void copyName(char *dest, const char *src) {
  int i = 0;
  while (src[i] && i < NAME_MAX) {
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
  for (int i = 0; i < stored.length() && n < NAME_MAX; i++) {
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

// Phone selectors use 1, 2, and 3. Anything else, including 0, prints a dash.
static void paintChoice(uint8_t value, const char *one, const char *two, const char *three) {
  if (value == 1) {
    lcd.print(one);
  } else if (value == 2) {
    lcd.print(two);
  } else if (value == 3) {
    lcd.print(three);
  } else {
    lcd.print("-");
  }
}

// Redraw only the strip along the bottom. "Ready" means no phone is
// connected. A connected phone shows the student and response it picked.
// The shown* variables remember what we drew, so loop() can skip this
// when the phone has not changed anything.
static void paintStatus() {
  lcd.fillRect(0, 420, 800, 60, COL_FOOT);
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(COL_FG, COL_FOOT);
  lcd.setCursor(24, 440);
  if (RemoteXY.connect_flag) {
    paintChoice(RemoteXY.selectorSwitch_01, "Student 1", "Student 2", "Student 3");
    lcd.print("    ");
    paintChoice(RemoteXY.optionSelector_01, "Response 1", "Response 2", "Response 3");
  } else {
    lcd.print("Ready");
  }
  shownConnect = RemoteXY.connect_flag;
  shownSwitch = RemoteXY.selectorSwitch_01;
  shownOption = RemoteXY.optionSelector_01;
}

// How many response buttons belong to the feeling that was tapped.
static int submenuCount() {
  switch (selectedMenu) {
    case 0: return anxietyCount;
    case 1: return boredomCount;
    case 2: return understandingCount;
    case 3: return angerCount;
    case 4: return uncomfortableCount;
  }
  return 0;
}

// The response at this row for the feeling currently open.
// A bad feeling index falls back to the first worried response
// so the screen still has something safe to draw.
static const Choice *choiceAt(int position) {
  switch (selectedMenu) {
    case 0: return &anxietyMenu[position];
    case 1: return &boredomMenu[position];
    case 2: return &understandingMenu[position];
    case 3: return &angerMenu[position];
    case 4: return &uncomfortableMenu[position];
  }
  return &anxietyMenu[0];
}

// Share the list area evenly across however many buttons there are.
// Cap the height so a short list, like Boredom's three items, does
// not turn into three huge bars.
static int rowHeightFor(int count) {
  int gaps = (count > 1) ? (count - 1) * ROW_GAP : 0;
  int height = (LIST_BOTTOM - LIST_TOP - gaps) / count;
  if (height > 64) {
    height = 64;
  }
  return height;
}

// Pixel y of button number index. count matters because the buttons
// get shorter when there are more of them.
static int rowTopFor(int index, int count) {
  return LIST_TOP + index * (rowHeightFor(count) + ROW_GAP);
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
//     paintMainMenu()    the five feelings
//     paintSubmenu()     green and orange response buttons
//     paintSelected()    the instructions for one response
//     paintSettings()    name, brightness, and grade level
//     paintNameEditor()  the keyboard
//   The words themselves are in the text-and-colors block above.
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
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_BG);
  char greeting[NAME_MAX + 8];
  snprintf(greeting, sizeof(greeting), "Hi, %s", studentName);
  lcd.drawString(greeting, 400, 180);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("How do you feel?", 400, 240);
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// The five feelings. Each row is one entry in menu[].
static void paintMainMenu() {
  int height = rowHeightFor(totalLines);
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("How do you feel?", 24, 32);
  for (int i = 0; i < totalLines; i++) {
    int top = rowTopFor(i, totalLines);
    lcd.fillRoundRect(ROW_X, top, ROW_W, height, 12, COL_ROW);
    lcd.setTextColor(COL_FG, COL_ROW);
    lcd.drawString(menu[i], ROW_X + 24, top + height / 2);
  }
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// Responses for the open feeling. Green rows can be tried alone.
// Orange rows ask the teacher. The words on the right say which is which.
static void paintSubmenu() {
  int count = submenuCount();
  int height = rowHeightFor(count);
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString(menu[selectedMenu], 24, 32);
  paintBackButton();
  for (int i = 0; i < count; i++) {
    const Choice *choice = choiceAt(i);
    int top = rowTopFor(i, count);
    uint16_t fill = choice->asksTeacher ? COL_ASK : COL_TRY;
    lcd.fillRoundRect(ROW_X, top, ROW_W, height, 12, fill);
    lcd.setTextDatum(textdatum_t::middle_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, fill);
    lcd.drawString(choice->label, ROW_X + 16, top + height / 2);
    lcd.setTextDatum(textdatum_t::middle_right);
    lcd.drawString(choice->asksTeacher ? "Ask teacher" : "Try this", ROW_X + ROW_W - 16, top + height / 2);
  }
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

// Draw text that wraps onto new lines. A \n in the string forces a
// break even when the line is not full. Words are copied one at a time
// into buf because drawString needs a normal C string, not a slice.
static void drawWrapped(const char *text, int x, int y, int maxWidth) {
  lcd.setTextDatum(textdatum_t::top_left);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.setFont(&fonts::Font4);
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

// The detail page for one response: a colored banner, the short label,
// then the longer instructions.
static void paintSelected() {
  const Choice *choice = choiceAt(submenuPos);
  uint16_t band = choice->asksTeacher ? COL_ASK : COL_TRY;
  lcd.fillScreen(COL_BG);
  paintBackButton();
  lcd.fillRoundRect(24, 64, 752, 64, 12, band);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, band);
  lcd.drawString(choice->asksTeacher ? "Ask my teacher" : "Try this", 44, 96);
  lcd.setTextColor(COL_FG, COL_BG);
  drawWrapped(choice->label, 24, 150, 752);
  drawWrapped(choice->help, 24, 210, 752);
  paintStatus();
  paintGearButton();
  lcd.setTextDatum(textdatum_t::top_left);
}

static int gradeRowTop(int index) {
  return GRADE_TOP + index * (GRADE_H + GRADE_GAP);
}

// Settings page. Brightness and grade save as soon as they change.
// The name is only saved from the keyboard screen.
// Green on a grade row means that row is selected and it is the live layout.
// Orange means it is selected but still a placeholder.
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
  lcd.drawString("Grade level", 24, GRADE_NOTE_Y);
  lcd.setTextDatum(textdatum_t::top_right);
  if (gradeOptions[gradeLevel].live) {
    lcd.drawString("Students see this screen now.", 776, GRADE_NOTE_Y);
  } else {
    lcd.drawString("Saved. Student screen stays Grades 2-4.", 776, GRADE_NOTE_Y);
  }

  for (int i = 0; i < GRADE_COUNT; i++) {
    int top = gradeRowTop(i);
    bool selected = (i == gradeLevel);
    uint16_t fill = COL_ROW;
    if (selected && gradeOptions[i].live) {
      fill = COL_TRY;
    } else if (selected) {
      fill = COL_ASK;
    }
    lcd.fillRoundRect(ROW_X, top, ROW_W, GRADE_H, 12, fill);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(COL_FG, fill);
    lcd.drawString(gradeOptions[i].label, ROW_X + 16, top + 6);
    lcd.drawString(gradeOptions[i].about, ROW_X + 16, top + 30);
    lcd.setTextDatum(textdatum_t::middle_right);
    const char *tag = gradeOptions[i].live ? "In use" : "Placeholder";
    lcd.drawString(tag, ROW_X + ROW_W - 16, top + GRADE_H / 2);
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

// Keyboard page. Save is green only when the name is not empty.
// Back throws away what was typed and returns to settings.
static void paintNameEditor() {
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Student name", 24, 30);
  paintTopButton(NAME_BACK_X, NAME_BACK_Y, NAME_BACK_W, NAME_BACK_H, "Back", COL_ROW);
  uint16_t saveFill = nameEdit[0] ? COL_TRY : COL_ROW;
  paintTopButton(DONE_X, DONE_Y, DONE_W, DONE_H, "Save", saveFill);

  lcd.fillRoundRect(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, 12, COL_ROW);
  lcd.setTextDatum(textdatum_t::middle_left);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_ROW);
  int midY = FIELD_Y + FIELD_H / 2;
  if (nameEdit[0] == 0) {
    lcd.setFont(&fonts::Font2);
    lcd.drawString("Tap letters below", FIELD_X + 20, midY);
  } else {
    lcd.drawString(nameEdit, FIELD_X + 20, midY);
    int textW = lcd.textWidth(nameEdit);
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
  int n = strlen(nameEdit);
  if (n == 0) {
    return true;
  }
  char prev = nameEdit[n - 1];
  return prev == ' ' || prev == '-' || prev == '\'';
}

// Change nameEdit for one key, then redraw. A key that cannot be
// used (name already full, or a second space in a row) returns
// without redrawing.
static void applyKey(const KeyHit *key) {
  int n = strlen(nameEdit);
  if (key->action == KEY_BACKSPACE) {
    if (n > 0) {
      nameEdit[n - 1] = 0;
    }
  } else if (key->action == KEY_CLEAR) {
    nameEdit[0] = 0;
  } else if (key->action == KEY_CHAR || key->action == KEY_SPACE) {
    char c = (key->action == KEY_SPACE) ? ' ' : key->ch;
    if (n >= NAME_MAX) {
      return;
    }
    if ((c == ' ' || c == '-' || c == '\'') && (n == 0 || nameEdit[n - 1] == c)) {
      return;
    }
    // Keys are labeled in capitals. Subtracting 'A' and adding 'a'
    // turns that capital into the lowercase letter, unless this
    // spot is the start of a word.
    if (c >= 'A' && c <= 'Z' && !nameNeedsCapital()) {
      c = c - 'A' + 'a';
    }
    nameEdit[n] = c;
    nameEdit[n + 1] = 0;
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
  currentScreen = SCREEN_MENU;
  paintMainMenu();
}

// Open the response list for feeling number index.
static void openSubmenu(int index) {
  selectedMenu = index;
  submenuPos = 0;
  currentScreen = SCREEN_SUBMENU;
  paintSubmenu();
}

// Open the detail page for response number index in the current list.
static void showSelectedItem(int index) {
  submenuPos = index;
  currentScreen = SCREEN_SELECTED;
  paintSelected();
}

// Detail page goes back to its response list. The response list
// goes back to the five feelings. Settings does not use this.
static void goBack() {
  if (currentScreen == SCREEN_SELECTED) {
    currentScreen = SCREEN_SUBMENU;
    paintSubmenu();
  } else {
    showMainMenu();
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
  if (dest == SCREEN_SUBMENU) {
    paintSubmenu();
  } else if (dest == SCREEN_SELECTED) {
    paintSelected();
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
  copyName(nameEdit, studentName);
  currentScreen = SCREEN_NAME;
  paintNameEditor();
}

// Drop the typed name and go back to settings.
static void closeNameEditor() {
  currentScreen = SCREEN_SETTINGS;
  paintSettings();
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

// Save which grade row was tapped and redraw. The student screens
// do not switch yet. Only the settings page shows the new choice.
static void setGrade(int index) {
  if (index < 0 || index >= GRADE_COUNT || index == gradeLevel) {
    return;
  }
  gradeLevel = index;
  saveSettings();
  paintSettings();
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
  applyBrightness();

  RemoteXY_Init();       // start the Wi-Fi hotspot and the phone listener
  screenMs = millis();    // start the two-second greeting timer
  paintHello();
}

// Repeats forever. Each pass gives the phone a turn, maybe advances
// the greeting, refreshes the footer if the phone changed, then
// handles at most one new finger tap.
void loop() {
  // Must run every pass. If it is skipped, the phone app disconnects.
  RemoteXYEngine.handler();

  // Greeting timer. Opening settings leaves SCREEN_HELLO, so the timer
  // does not fire while the teacher is still in settings.
  if (currentScreen == SCREEN_HELLO && (millis() - screenMs) >= HELLO_MS) {
    showMainMenu();
  }

  // Settings and the keyboard do not use the phone footer. Drawing it
  // there would cover the bottom of those pages.
  if (currentScreen != SCREEN_HELLO &&
      currentScreen != SCREEN_SETTINGS &&
      currentScreen != SCREEN_NAME &&
      (RemoteXY.connect_flag != shownConnect ||
       RemoteXY.selectorSwitch_01 != shownSwitch ||
       RemoteXY.optionSelector_01 != shownOption)) {
    paintStatus();
  }

  // getTouch is true the whole time a finger is down, and it fills tx, ty.
  // touchWasDown remembers the previous pass, so we act only on the
  // moment the finger first lands. TAP_GAP_MS blocks a bounce right after.
  int32_t tx = 0;
  int32_t ty = 0;
  bool touchDown = lcd.getTouch(&tx, &ty);
  if (touchDown && !touchWasDown && (millis() - tapMs) >= TAP_GAP_MS) {
    tapMs = millis();
    bool onGearScreen = currentScreen == SCREEN_HELLO ||
                        currentScreen == SCREEN_MENU ||
                        currentScreen == SCREEN_SUBMENU ||
                        currentScreen == SCREEN_SELECTED;
    // The gear is checked before the rest of the page. On the greeting,
    // any other tap skips ahead to the feelings list.
    if (onGearScreen && inBox(tx, ty, GEAR_X, GEAR_Y, GEAR_W, GEAR_H)) {
      openSettings();
    } else if (currentScreen == SCREEN_HELLO) {
      showMainMenu();
    // Feelings list: the row under the finger opens that feeling.
    } else if (currentScreen == SCREEN_MENU) {
      int height = rowHeightFor(totalLines);
      for (int i = 0; i < totalLines; i++) {
        if (inBox(tx, ty, ROW_X, rowTopFor(i, totalLines), ROW_W, height)) {
          openSubmenu(i);
          break;
        }
      }
    // Response list: Back, or the row under the finger.
    } else if (currentScreen == SCREEN_SUBMENU) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        goBack();
      } else {
        int count = submenuCount();
        int height = rowHeightFor(count);
        for (int i = 0; i < count; i++) {
          if (inBox(tx, ty, ROW_X, rowTopFor(i, count), ROW_W, height)) {
            showSelectedItem(i);
            break;
          }
        }
      }
    // Detail page: only Back does anything. Other taps are ignored.
    } else if (currentScreen == SCREEN_SELECTED) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        goBack();
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
        saveNameEdit();
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
  touchWasDown = touchDown;
}
