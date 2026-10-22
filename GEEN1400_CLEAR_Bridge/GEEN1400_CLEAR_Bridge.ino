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
*/

#define REMOTEXY_MODE__WIFI_POINT

#include <WiFi.h>

#define REMOTEXY_WIFI_SSID "RemoteXY"
#define REMOTEXY_WIFI_PASSWORD "12345678"
#define REMOTEXY_SERVER_PORT 6377

#include <RemoteXY.h>
#include <Wire.h>
#include <Preferences.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

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

struct {
  uint8_t selectorSwitch_01; // student, from 0 to 3
  uint8_t optionSelector_01; // response, from 0 to 3
  uint8_t strings_01; // from 0 to 1
  uint8_t strings_02; // from 0 to 1
  uint8_t strings_03; // from 0 to 1
  uint8_t connect_flag;  // =1 if wire connected, else =0
} RemoteXY;
#pragma pack(pop)

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Touch_GT911 _touch_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void) {
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

static const uint8_t PIN_BL = 2;
static const uint8_t PCA9557_ADDR = 0x19;
static const unsigned long HELLO_MS = 2000;
static const unsigned long TAP_GAP_MS = 200;

static const int SCREEN_HELLO = 0;
static const int SCREEN_MENU = 1;
static const int SCREEN_SUBMENU = 2;
static const int SCREEN_SELECTED = 3;
static const int SCREEN_SETTINGS = 4;
static const int SCREEN_NAME = 5;

static const int ROW_X = 24;
static const int ROW_W = 752;
static const int ROW_GAP = 6;
static const int LIST_TOP = 64;
static const int LIST_BOTTOM = 412;
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
static const int BRIGHT_MIN = 20;
static const int BRIGHT_MAX = 100;

static const int GRADE_NOTE_Y = 206;
static const int GRADE_TOP = 230;
static const int GRADE_H = 56;
static const int GRADE_GAP = 6;
static const int GRADE_COUNT = 4;
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

static const int KEY_CHAR = 1;
static const int KEY_BACKSPACE = 2;
static const int KEY_CLEAR = 3;
static const int KEY_SPACE = 4;

static const uint16_t COL_BG = 0xF7BD;
static const uint16_t COL_FG = 0x2610;
static const uint16_t COL_ROW = 0x6F5F;
static const uint16_t COL_TRY = 0x6F9B;
static const uint16_t COL_ASK = 0xFE70;
static const uint16_t COL_FOOT = 0xE73C;

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

struct Choice {
  const char *label;
  const char *help;
  bool asksTeacher;
};

static const char *menu[] = {
  "Worried",
  "Bored",
  "I don't get it",
  "Mad",
  "Uncomfortable"
};
static const Choice anxietyMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"I need a walk", "Ask your teacher if you can take a short walk.", true},
  {"Help me calm down", "Sit down.\nPut your feet on the floor.\nOpen your hands.\nBreathe in, then breathe out.\nDo this 5 times.", false},
  {"Talk to someone", "Ask your teacher if you can talk with someone.", true},
  {"Breathing", "Breathe in while you count to 4.\nHold for 2.\nBreathe out while you count to 4.\nDo this 5 times.", false},
  {"Talk after class", "Ask to meet your teacher after class.", true}
};
static const Choice boredomMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"Draw how I feel", "Draw a picture of how you feel. You can show it when you are ready.", false},
  {"Quiet looking game", "Find 5 blue things.\nThen find 4 round things.\nThen find 3 soft things.", false}
};
static const Choice understandingMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"Draw the problem", "Draw the problem.\nCircle the part you do not understand.", false},
  {"Please say that again", "Ask your teacher to explain the last part again.", true},
  {"Explain this question", "Ask your teacher to explain this question.", true},
  {"Can we practice one?", "Ask for one example or practice problem.", true}
};
static const Choice angerMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"I need a walk", "Ask your teacher if you can take a short walk.", true},
  {"Breathing", "Breathe in while you count to 4.\nHold for 2.\nBreathe out while you count to 4.\nDo this 5 times.", false},
  {"Talk to someone", "Ask your teacher if you can talk with someone.", true},
  {"Help me cool down", "Stop.\nPut your hands in your lap.\nCount slowly to 10.\nTake one slow breath.", false}
};
static const Choice uncomfortableMenu[] = {
  {"Write my own words", "Your teacher can say yes or no. This will go to the teacher phone later.", true},
  {"I need a walk", "Ask your teacher if you can take a short walk.", true},
  {"Please help me", "Ask your teacher for help with this problem.", true},
  {"Can I see an example?", "Ask your teacher to show you one example.", true}
};

static const int totalLines = 5;
static const int anxietyCount = 6;
static const int boredomCount = 3;
static const int understandingCount = 5;
static const int angerCount = 5;
static const int uncomfortableCount = 4;

struct KeyHit {
  int action;
  char ch;
  int x;
  int y;
  int w;
  int h;
};

static LGFX lcd;
static Preferences prefs;
static bool expanderOk = false;
static int currentScreen = SCREEN_HELLO;
static int returnScreen = SCREEN_MENU;
static int selectedMenu = 0;
static int submenuPos = 0;
static unsigned long screenMs = 0;
static unsigned long tapMs = 0;
static bool touchWasDown = false;
static bool nameDirty = false;
static uint8_t shownConnect = 0xFF;
static uint8_t shownSwitch = 0xFF;
static uint8_t shownOption = 0xFF;
static char studentName[NAME_MAX + 1] = "Connor";
static char nameEdit[NAME_MAX + 1] = "Connor";
static int brightnessPct = 80;
static int gradeLevel = GRADE_LIVE;

static bool pcaWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(PCA9557_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

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

static void copyName(char *dest, const char *src) {
  int i = 0;
  while (src[i] && i < NAME_MAX) {
    dest[i] = src[i];
    i++;
  }
  dest[i] = 0;
}

static int dutyForBrightness(int pct) {
  if (pct < BRIGHT_MIN) {
    pct = BRIGHT_MIN;
  }
  if (pct > BRIGHT_MAX) {
    pct = BRIGHT_MAX;
  }
  return 50 + (205 * (pct - BRIGHT_MIN)) / (BRIGHT_MAX - BRIGHT_MIN);
}

static void applyBrightness() {
  lcd.setBrightness(dutyForBrightness(brightnessPct));
  if (expanderOk) {
    pcaWrite(0x01, 0x01);
  }
}

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
  brightnessPct = (brightnessPct / 10) * 10;
  if (gradeLevel < 0 || gradeLevel >= GRADE_COUNT) {
    gradeLevel = GRADE_LIVE;
  }
}

static void saveSettings() {
  prefs.begin("clear", false);
  prefs.putString("name", studentName);
  prefs.putInt("bright", brightnessPct);
  prefs.putInt("grade", gradeLevel);
  prefs.end();
}

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

static int rowHeightFor(int count) {
  int gaps = (count > 1) ? (count - 1) * ROW_GAP : 0;
  int height = (LIST_BOTTOM - LIST_TOP - gaps) / count;
  if (height > 64) {
    height = 64;
  }
  return height;
}

static int rowTopFor(int index, int count) {
  return LIST_TOP + index * (rowHeightFor(count) + ROW_GAP);
}

static bool inBox(int32_t x, int32_t y, int boxX, int boxY, int boxW, int boxH) {
  return x >= boxX && x < boxX + boxW && y >= boxY && y < boxY + boxH;
}

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
    if (cursorX > x && cursorX + width > x + maxWidth) {
      cursorX = x;
      cursorY += lineHeight;
    }
    lcd.drawString(buf, cursorX, cursorY);
    cursorX += width + lcd.textWidth(" ");
    word = end;
  }
}

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

static bool nameNeedsCapital() {
  int n = strlen(nameEdit);
  if (n == 0) {
    return true;
  }
  char prev = nameEdit[n - 1];
  return prev == ' ' || prev == '-' || prev == '\'';
}

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

static void showMainMenu() {
  currentScreen = SCREEN_MENU;
  paintMainMenu();
}

static void openSubmenu(int index) {
  selectedMenu = index;
  submenuPos = 0;
  currentScreen = SCREEN_SUBMENU;
  paintSubmenu();
}

static void showSelectedItem(int index) {
  submenuPos = index;
  currentScreen = SCREEN_SELECTED;
  paintSelected();
}

static void goBack() {
  if (currentScreen == SCREEN_SELECTED) {
    currentScreen = SCREEN_SUBMENU;
    paintSubmenu();
  } else {
    showMainMenu();
  }
}

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

static void openSettings() {
  returnScreen = currentScreen;
  currentScreen = SCREEN_SETTINGS;
  paintSettings();
}

static void openNameEditor() {
  copyName(nameEdit, studentName);
  currentScreen = SCREEN_NAME;
  paintNameEditor();
}

static void closeNameEditor() {
  currentScreen = SCREEN_SETTINGS;
  paintSettings();
}

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

static void setGrade(int index) {
  if (index < 0 || index >= GRADE_COUNT || index == gradeLevel) {
    return;
  }
  gradeLevel = index;
  saveSettings();
  paintSettings();
}

void setup() {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  initExpander();
  loadSettings();
  lcd.init();
  lcd.setRotation(0);
  applyBrightness();

  RemoteXY_Init();
  screenMs = millis();
  paintHello();
}

void loop() {
  RemoteXYEngine.handler();

  if (currentScreen == SCREEN_HELLO && (millis() - screenMs) >= HELLO_MS) {
    showMainMenu();
  }

  if (currentScreen != SCREEN_HELLO &&
      currentScreen != SCREEN_SETTINGS &&
      currentScreen != SCREEN_NAME &&
      (RemoteXY.connect_flag != shownConnect ||
       RemoteXY.selectorSwitch_01 != shownSwitch ||
       RemoteXY.optionSelector_01 != shownOption)) {
    paintStatus();
  }

  int32_t tx = 0;
  int32_t ty = 0;
  bool touchDown = lcd.getTouch(&tx, &ty);
  if (touchDown && !touchWasDown && (millis() - tapMs) >= TAP_GAP_MS) {
    tapMs = millis();
    bool onGearScreen = currentScreen == SCREEN_HELLO ||
                        currentScreen == SCREEN_MENU ||
                        currentScreen == SCREEN_SUBMENU ||
                        currentScreen == SCREEN_SELECTED;
    if (onGearScreen && inBox(tx, ty, GEAR_X, GEAR_Y, GEAR_W, GEAR_H)) {
      openSettings();
    } else if (currentScreen == SCREEN_HELLO) {
      showMainMenu();
    } else if (currentScreen == SCREEN_MENU) {
      int height = rowHeightFor(totalLines);
      for (int i = 0; i < totalLines; i++) {
        if (inBox(tx, ty, ROW_X, rowTopFor(i, totalLines), ROW_W, height)) {
          openSubmenu(i);
          break;
        }
      }
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
    } else if (currentScreen == SCREEN_SELECTED) {
      if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
        goBack();
      }
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
  touchWasDown = touchDown;
}
