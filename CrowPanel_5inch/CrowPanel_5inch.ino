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
*/

#define REMOTEXY_MODE__WIFI_POINT

#include <WiFi.h>

#define REMOTEXY_WIFI_SSID "RemoteXY"
#define REMOTEXY_WIFI_PASSWORD "12345678"
#define REMOTEXY_SERVER_PORT 6377

#include <RemoteXY.h>
#include <Wire.h>
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

static const int ROW_X = 24;
static const int ROW_W = 752;
static const int ROW_GAP = 6;
static const int LIST_TOP = 64;
static const int LIST_BOTTOM = 412;
static const int BACK_X = 600;
static const int BACK_Y = 12;
static const int BACK_W = 176;
static const int BACK_H = 40;

static const uint16_t COL_BG = 0xF7BD;
static const uint16_t COL_FG = 0x2610;
static const uint16_t COL_ROW = 0x6F5F;
static const uint16_t COL_TRY = 0x6F9B;
static const uint16_t COL_ASK = 0xFE70;
static const uint16_t COL_FOOT = 0xE73C;

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

static LGFX lcd;
static bool expanderOk = false;
static int currentScreen = SCREEN_HELLO;
static int selectedMenu = 0;
static int submenuPos = 0;
static unsigned long screenMs = 0;
static unsigned long tapMs = 0;
static bool touchWasDown = false;
static uint8_t shownConnect = 0xFF;
static uint8_t shownSwitch = 0xFF;
static uint8_t shownOption = 0xFF;

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

static void setBacklight(bool on) {
  digitalWrite(PIN_BL, on ? HIGH : LOW);
  if (expanderOk) {
    pcaWrite(0x01, on ? 0x01 : 0x00);
  }
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

static void paintBackButton() {
  lcd.fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 8, COL_ROW);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setTextColor(COL_FG, COL_ROW);
  lcd.drawString("Back", BACK_X + BACK_W / 2, BACK_Y + BACK_H / 2);
}

static void paintHello() {
  lcd.fillScreen(COL_BG);
  lcd.setTextDatum(textdatum_t::middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(COL_FG, COL_BG);
  lcd.drawString("Hi, Connor", 400, 180);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("How do you feel?", 400, 240);
  paintStatus();
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
  lcd.setTextDatum(textdatum_t::top_left);
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

void setup() {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  initExpander();
  lcd.init();
  lcd.setRotation(0);
  setBacklight(true);

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
    if (currentScreen == SCREEN_HELLO) {
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
    } else if (inBox(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) {
      goBack();
    }
  }
  touchWasDown = touchDown;
}
