/*
  GEEN1400 — Scott Nickerson
  Arduino Uno, 16x2 HD44780 (parallel), HM-10 BLE, RemoteXY app.

  Pin map from the Tinkercad sheet (USB on the left). Header order is
  AREF, GND, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, TX, RX.

  LCD, 4-bit. RW is a black wire to GND. D0–D3 are open.
    RS = 12   orange
    E  = 11   yellow
    D4 = 5    green
    D5 = 4    cyan
    D6 = 3    blue
    D7 = 2    purple
    VSS / VDD = GND / 5V
    V0 = contrast pot wiper (pink). Pot ends are 5V and GND. No Uno pin.
    LED anode (brown) goes through a resistor to 5V. Cathode (black) is GND.
    The backlight is not on a GPIO, so "off" is the HD44780 display command.

  Button on D13, active HIGH. Magenta jumper is the signal. The other
  side of the switch is a red jumper to 5V. A 10k (brown-black-orange)
  in the signal column goes up to the ground rail, so this is an external
  pulldown: pinMode INPUT, pressed == HIGH. D13 also lights the onboard LED
  while the button is held. Each press toggles the characters: on runs the
  count, off sends the HD44780 display-off command. The backlight stays lit
  either way, because it is wired to 5V.
  If a press acts on release instead of press, the 10k is on the 5V side —
  change the HIGH test in loop() to LOW.

  HM-10 is not drawn. The generated sketch had SoftwareSerial on D2/D3,
  which are LCD D7 and D6 here, so the module has to move:
    Uno D8 RX  <-  HM-10 TX
    Uno D10 TX ->  HM-10 RX
    9600 baud (HM-10 default)
  D6, D7, D9, and A0–A5 are still free. Leave D0/D1 for USB.
  Many HM-10 RX pins are not 5V tolerant; a 1k series resistor on D10 is wise.

  RemoteXY GUI bytes below are the editor export (129 bytes, V19).
  Inputs: selectorSwitch_01, optionSelector_01. Outputs: strings_01/04/02.
  The count runs on its own. App data is still read in loop(); it does not
  hold the LCD on a static screen.
*/

//////////////////////////////////////////////
//        RemoteXY include library          //
//////////////////////////////////////////////

// you can enable debug logging to Serial at 115200
//#define REMOTEXY__DEBUGLOG

// RemoteXY select connection mode and include library
#define REMOTEXY_MODE__SOFTSERIAL

#include <SoftwareSerial.h>

// RemoteXY connection settings
// D2/D3 from the editor collide with LCD D7/D6. HM-10 is on D8/D10.
#define REMOTEXY_SERIAL_RX 8
#define REMOTEXY_SERIAL_TX 10
#define REMOTEXY_SERIAL_SPEED 9600


#include <RemoteXY.h>

// RemoteXY GUI configuration
#pragma pack(push, 1)
uint8_t const PROGMEM RemoteXY_CONF_PROGMEM[] =   // 129 bytes V19
  { 255,2,0,3,0,122,0,19,0,0,0,0,31,1,106,200,1,1,5,0,
  3,18,86,22,60,3,4,30,74,54,90,46,10,12,2,16,6,64,82,101,
  115,112,111,110,115,101,32,49,0,74,54,110,46,10,12,2,16,6,64,82,
  101,115,112,111,110,115,101,32,50,0,74,54,132,46,10,12,2,16,6,64,
  82,101,115,112,111,110,115,101,32,51,0,12,3,10,51,10,193,30,15,83,
  116,117,100,101,110,116,32,49,0,83,116,117,100,101,110,116,32,50,0,83,
  116,117,100,101,110,116,32,51,0 };

// this structure defines all the variables and events of your control interface
struct {

    // input variables
  uint8_t selectorSwitch_01; // from 0 to 3
  uint8_t optionSelector_01; // from 0 to 3

    // output variables
  uint8_t strings_01; // from 0 to 1
  uint8_t strings_04; // from 0 to 1
  uint8_t strings_02; // from 0 to 1

    // other variable
  uint8_t connect_flag;  // =1 if wire connected, else =0

} RemoteXY;
#pragma pack(pop)

/////////////////////////////////////////////
//           END RemoteXY include          //
/////////////////////////////////////////////

#include <LiquidCrystal.h>

static const uint8_t PIN_BUTTON = 7;
static const unsigned long DEBOUNCE_MS = 45;
static const unsigned long DIGIT_MS = 700;
static const unsigned long MSG_MS = 1400;
static const unsigned long SWEEP_MS = 140;

static const uint8_t PHASE_WAIT = 0;
static const uint8_t PHASE_COUNT = 1;
static const uint8_t PHASE_MSG = 2;
static const uint8_t PHASE_SWEEP = 3;

// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

static uint8_t smile[8] = {
  0b00000,
  0b01010,
  0b01010,
  0b00000,
  0b10001,
  0b01110,
  0b00000,
  0b00000
};

static uint8_t phase = PHASE_WAIT;
static uint8_t digit = 0;
static uint8_t msg = 0;
static uint8_t sweep = 0;
static unsigned long phaseMs = 0;

static bool rawLevel = LOW;
static bool stableLevel = LOW;
static unsigned long changeMs = 0;
static bool screenOn = false;

void paintCount() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Count: "));
  lcd.print(digit);
  lcd.print(F(" / 9"));
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < 10; i++) {
    lcd.write(i <= digit ? (uint8_t)255 : (uint8_t)'-');
  }
}

void paintMsg() {
  lcd.clear();
  if (msg == 0) {
    lcd.setCursor(0, 0);
    lcd.print(F("0-9 complete"));
    lcd.setCursor(0, 1);
    lcd.print(F("nice work"));
  } else if (msg == 1) {
    lcd.setCursor(0, 0);
    lcd.print(F("GEEN1400 "));
    lcd.write((uint8_t)0);
    lcd.setCursor(0, 1);
    lcd.print(F("Uno is ready"));
  } else {
    lcd.setCursor(0, 0);
    lcd.print(F("HM-10  D8 / D10"));
    lcd.setCursor(0, 1);
    lcd.print(F("9600  app on"));
  }
}

void paintSweep() {
  if (sweep == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Still listening"));
  }
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < 16; i++) {
    lcd.write(i == sweep ? (uint8_t)255 : (uint8_t)' ');
  }
}

void startScreen() {
  screenOn = true;
  phase = PHASE_COUNT;
  digit = 0;
  phaseMs = millis();
  lcd.display();
  paintCount();
}

void stopScreen() {
  screenOn = false;
  phase = PHASE_WAIT;
  lcd.noDisplay();
}

void setup() {
  pinMode(PIN_BUTTON, INPUT);

  lcd.begin(16, 2);
  lcd.createChar(0, smile);
  lcd.clear();

  rawLevel = digitalRead(PIN_BUTTON);
  stableLevel = rawLevel;
  changeMs = millis();

  RemoteXY_Init();
  startScreen();
}

void loop() {
  RemoteXYEngine.handler();

  bool reading = digitalRead(PIN_BUTTON);
  if (reading != rawLevel) {
    rawLevel = reading;
    changeMs = millis();
  }
  if ((millis() - changeMs) >= DEBOUNCE_MS && stableLevel != rawLevel) {
    stableLevel = rawLevel;
    if (stableLevel == HIGH) {
      if (screenOn) {
        stopScreen();
      } else {
        startScreen();
      }
    }
  }

  if (!screenOn) {
    return;
  }

  unsigned long now = millis();
  if (phase == PHASE_COUNT) {
    if (now - phaseMs >= DIGIT_MS) {
      phaseMs = now;
      if (digit < 9) {
        digit++;
        paintCount();
      } else {
        phase = PHASE_MSG;
        msg = 0;
        paintMsg();
      }
    }
  } else if (phase == PHASE_MSG) {
    if (now - phaseMs >= MSG_MS) {
      phaseMs = now;
      if (msg < 2) {
        msg++;
        paintMsg();
      } else {
        phase = PHASE_SWEEP;
        sweep = 0;
        paintSweep();
      }
    }
  } else if (now - phaseMs >= SWEEP_MS) {
    phaseMs = now;
    if (sweep < 15) {
      sweep++;
      paintSweep();
    } else {
      phase = PHASE_MSG;
      msg = 0;
      paintMsg();
    }
  }
}
