/*
  GEEN1400 — Scott Nickerson
  Arduino Uno, 16x2 HD44780 (parallel), HM-10 BLE, RemoteXY app.

  This is the earlier demo. The CrowPanel sketch in GEEN1400_CLEAR_Bridge
  is the one students use now. This file still shows the same idea on a
  small two-line character screen: the board runs a short animation, a
  button hides or shows the letters, and a phone can connect.

  How an Arduino sketch runs:
  setup() runs once when the board turns on or is reset.
  loop() then repeats forever. Anything that must keep happening
  (watch the button, talk to the phone, advance the animation)
  belongs in loop(). This sketch avoids delay(), because delay()
  would freeze the phone connection while it waits.

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
    That command hides the letters. The light behind them stays on.

  Button on D13, active HIGH. Magenta jumper is the signal. The other
  side of the switch is a red jumper to 5V. A 10k (brown-black-orange)
  in the signal column goes up to the ground rail, so this is an external
  pulldown: pinMode INPUT, pressed == HIGH. D13 also lights the onboard LED
  while the button is held. Each press toggles the characters: on runs the
  count, off sends the HD44780 display-off command. The backlight stays lit
  either way, because it is wired to 5V.
  If a press acts on release instead of press, the 10k is on the 5V side —
  change the HIGH test in loop() to LOW.
  The program itself reads pin 7 (PIN_BUTTON below), not pin 13.

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

// Uncomment the next line to print connection details on the USB serial
// monitor at 115200 baud. Leave it commented for normal classroom use.
//#define REMOTEXY__DEBUGLOG

// SOFTSERIAL means the Bluetooth module uses two normal pins (D8 and D10)
// instead of the Uno's USB port. The USB port stays free for programming.
#define REMOTEXY_MODE__SOFTSERIAL

#include <SoftwareSerial.h>

// Which Uno pins talk to the HM-10, and how fast.
// The editor originally picked D2 and D3. Those pins are already the LCD,
// so the wires were moved to D8 (Uno listens) and D10 (Uno talks).
// 9600 is the speed the HM-10 uses unless someone changed it.
#define REMOTEXY_SERIAL_RX 8
#define REMOTEXY_SERIAL_TX 10
#define REMOTEXY_SERIAL_SPEED 9600


#include <RemoteXY.h>

// RemoteXY GUI configuration.
// The long list of numbers is a picture of the phone screen, exported
// from the RemoteXY editor. It is not meant to be edited by hand.
// Change the phone layout in the editor, then paste the new export here.
#pragma pack(push, 1)
uint8_t const PROGMEM RemoteXY_CONF_PROGMEM[] =   // 129 bytes V19
  { 255,2,0,3,0,122,0,19,0,0,0,0,31,1,106,200,1,1,5,0,
  3,18,86,22,60,3,4,30,74,54,90,46,10,12,2,16,6,64,82,101,
  115,112,111,110,115,101,32,49,0,74,54,110,46,10,12,2,16,6,64,82,
  101,115,112,111,110,115,101,32,50,0,74,54,132,46,10,12,2,16,6,64,
  82,101,115,112,111,110,115,101,32,51,0,12,3,10,51,10,193,30,15,83,
  116,117,100,101,110,116,32,49,0,83,116,117,100,101,110,116,32,50,0,83,
  116,117,100,101,110,116,32,51,0 };

// These names are the controls on the phone.
// The library fills them in. loop() can read them at any time.
// The packing lines keep the bytes in the exact order RemoteXY expects.
struct {

    // input variables. The phone sends these to the Uno.
    // 0 means nothing picked. 1, 2, and 3 are the three choices.
  uint8_t selectorSwitch_01; // from 0 to 3
  uint8_t optionSelector_01; // from 0 to 3

    // output variables. The Uno can send these back to the phone.
    // This demo does not change them. They stay at 0.
  uint8_t strings_01; // from 0 to 1
  uint8_t strings_04; // from 0 to 1
  uint8_t strings_02; // from 0 to 1

    // other variable.
    // 1 when the phone app is connected, 0 when it is not.
  uint8_t connect_flag;  // =1 if wire connected, else =0

} RemoteXY;
#pragma pack(pop)

/////////////////////////////////////////////
//           END RemoteXY include          //
/////////////////////////////////////////////

#include <LiquidCrystal.h>

// Pin the program actually reads for the button. HIGH means pressed,
// because a resistor holds the pin LOW until the switch connects 5V.
static const uint8_t PIN_BUTTON = 7;

// A real button can flicker for a few milliseconds as the metal contacts
// bounce. Wait this long on the same reading before trusting it.
static const unsigned long DEBOUNCE_MS = 45;

// How long each step of the on-screen animation stays up.
static const unsigned long DIGIT_MS = 700;  // one count, such as "3 / 9"
static const unsigned long MSG_MS = 1400;   // one of the three messages
static const unsigned long SWEEP_MS = 140;  // one step of the moving block

// The animation is a small state machine. phase says which part is running.
// WAIT  : letters are hidden. The board is idle until the button is pressed.
// COUNT : show 0, then 1, then 2, up through 9.
// MSG   : show three short text screens, one after another.
// SWEEP : move one solid block across the bottom row, then go back to MSG.
static const uint8_t PHASE_WAIT = 0;
static const uint8_t PHASE_COUNT = 1;
static const uint8_t PHASE_MSG = 2;
static const uint8_t PHASE_SWEEP = 3;

// The LCD is wired in 4-bit mode. The numbers are Arduino pins:
// RS, E, D4, D5, D6, D7. See the pin list at the top of the file.
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// A custom character is 8 rows of 5 pixels. A 1 bit is a dark pixel.
// This pattern is a simple smile. createChar() stores it in slot 0,
// and lcd.write(0) draws it later.
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

// Remember where the animation is between trips through loop().
// These would reset every time if they were created inside loop().
static uint8_t phase = PHASE_WAIT;
static uint8_t digit = 0;   // the number currently shown, 0 through 9
static uint8_t msg = 0;     // which of the three messages is showing
static uint8_t sweep = 0;   // column of the moving block, 0 through 15
static unsigned long phaseMs = 0;  // millis() when this step started

// Button memory. rawLevel is the latest pin reading, which may still be
// bouncing. stableLevel is the reading we have decided to trust.
static bool rawLevel = LOW;
static bool stableLevel = LOW;
static unsigned long changeMs = 0;  // millis() when the raw reading last changed
static bool screenOn = false;       // true while the letters are visible

// Draw the counting screen.
// Top row: "Count: 3 / 9". Bottom row: a bar of filled blocks, one per
// number reached, with dashes for the numbers still ahead.
// Character 255 is a solid block that the LCD already knows.
void paintCount() {
  lcd.clear();
  lcd.setCursor(0, 0);
  // F() keeps the text in flash instead of the Uno's small RAM.
  lcd.print(F("Count: "));
  lcd.print(digit);
  lcd.print(F(" / 9"));
  lcd.setCursor(0, 1);
  for (uint8_t i = 0; i < 10; i++) {
    lcd.write(i <= digit ? (uint8_t)255 : (uint8_t)'-');
  }
}

// Draw one of the three message screens. msg picks which one.
// The smile on the second message is the custom character in slot 0.
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

// Draw the moving block. The top line is written only on the first step,
// so it does not flicker. Each later call only redraws the bottom row.
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

// Button turned the letters on. Start the count over at 0.
// lcd.display() undoes lcd.noDisplay(). It does not turn on the backlight.
void startScreen() {
  screenOn = true;
  phase = PHASE_COUNT;
  digit = 0;
  phaseMs = millis();
  lcd.display();
  paintCount();
}

// Button turned the letters off. Stay in loop() so the phone is still
// serviced, but skip the animation until the button is pressed again.
void stopScreen() {
  screenOn = false;
  phase = PHASE_WAIT;
  lcd.noDisplay();
}

// Runs once. Prepare the button, the LCD, and the phone link, then
// show the first counting screen.
void setup() {
  // INPUT, not INPUT_PULLUP. The pulldown resistor is on the breadboard.
  pinMode(PIN_BUTTON, INPUT);

  // 16 columns, 2 rows. Then store the smile in custom-character slot 0.
  lcd.begin(16, 2);
  lcd.createChar(0, smile);
  lcd.clear();

  // Take one reading now so the first loop does not look like a new press.
  rawLevel = digitalRead(PIN_BUTTON);
  stableLevel = rawLevel;
  changeMs = millis();

  // Start listening for the RemoteXY phone app over the HM-10.
  RemoteXY_Init();
  startScreen();
}

// Runs over and over. Each pass does three jobs, in this order:
// 1. Give the phone library a turn, or the app will time out.
// 2. Debounce the button and toggle the letters on a real press.
// 3. If the letters are visible, advance the animation when its timer is up.
void loop() {
  RemoteXYEngine.handler();

  // Debounce. If the pin changed, start the wait over. Only accept the
  // new level after it has held still for DEBOUNCE_MS. Act on the change
  // to HIGH, which is the press, not the release.
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

  // Hidden screen: nothing left to draw this pass.
  if (!screenOn) {
    return;
  }

  // millis() counts milliseconds since startup. phaseMs is when the
  // current step began. The difference is how long that step has lasted.
  unsigned long now = millis();
  if (phase == PHASE_COUNT) {
    if (now - phaseMs >= DIGIT_MS) {
      phaseMs = now;
      if (digit < 9) {
        digit++;
        paintCount();
      } else {
        // The count finished. Start the first message.
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
        // All three messages have been shown. Start the moving block.
        phase = PHASE_SWEEP;
        sweep = 0;
        paintSweep();
      }
    }
  } else if (now - phaseMs >= SWEEP_MS) {
    // This branch is PHASE_SWEEP. The other two phases returned above.
    phaseMs = now;
    if (sweep < 15) {
      sweep++;
      paintSweep();
    } else {
      // The block reached the right edge. Play the messages again.
      phase = PHASE_MSG;
      msg = 0;
      paintMsg();
    }
  }
}
