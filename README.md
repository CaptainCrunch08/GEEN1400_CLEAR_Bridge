# GEEN1400 CLEAR Bridge

CLEAR is a classroom tool for grades K–5. A student picks how they feel, then either tries a short coping step or sends a request a teacher can approve later.

The current device is an Elecrow CrowPanel 5.0 inch display. The ESP32-S3 is already on that board. `GEEN1400_Prototype.ino` is the earlier Arduino Uno version, written for a 16x2 character LCD.

## What the student sees

The panel opens with “Hi, Connor” and then **How do you feel?**

- Worried
- Bored
- I don't get it
- Mad
- Uncomfortable

Each feeling opens a list of responses.

- **Green / Try this** is something the student can do alone, such as breathing, counting to 10, or a quiet looking game. The next screen gives the steps.
- **Orange / Ask teacher** is a request, such as a short walk, talking with someone, a clearer explanation, or an example. The screen says the teacher can say yes or no. The teacher phone app is not connected yet, so nothing is actually sent.

**Back** returns to the previous list.

## Teacher connection

The CrowPanel starts its own Wi-Fi network. This is not the school or home network.

- Network name: `RemoteXY`
- Password: `12345678`

Join that network from the phone, then open the RemoteXY app. The sketch expects RemoteXY library 4.1.10 or later, and the Android or iOS app versions noted by RemoteXY. The app GUI has a student selector and a response selector. When the app is connected, those choices show along the bottom of the panel.

## CrowPanel sketch

Open `GEEN1400_CLEAR_Bridge/GEEN1400_CLEAR_Bridge.ino` in the Arduino IDE.

Board settings:

- Board: ESP32S3 Dev Module
- PSRAM: OPI PSRAM
- Flash Size: 4MB (32Mb)
- Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
- Flash Mode: QIO 80MHz
- CPU Frequency: 240MHz (WiFi/BT)

Libraries, from the Arduino Library Manager:

- LovyanGFX 1.2 or later. The sketch includes the RGB panel headers directly, because that version does not expose them from `LovyanGFX.hpp`.
- RemoteXY 4.1.10 or later

`WiFi.h` and `Wire.h` come with the ESP32 board package. Install **esp32 by Espressif Systems** from the Boards Manager.

Program the board through the USB-C port on the display. The CrowPanel uses a CH340 USB chip. If no COM port appears, install the WCH CH340 driver and use a USB cable that carries data, not a charge-only cable. In Device Manager the port should show as **USB-SERIAL CH340 (COMx)**. If upload stays on “Connecting…”, hold **BOOT**, tap **RST**, and release **BOOT**.

## Uno prototype

`GEEN1400_Prototype.ino` targets an Arduino Uno, a 16x2 HD44780 LCD, a button on pin 13, and an HM-10 Bluetooth module for RemoteXY. The CrowPanel replaces that hardware. Leave the HM-10 unplugged when using the display board. The ESP32-S3 provides the Wi-Fi link instead.
