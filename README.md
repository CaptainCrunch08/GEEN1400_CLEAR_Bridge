# GEEN1400 CLEAR Bridge

CLEAR is a classroom support tool for grades K–5. A student enters a name, then chooses whether they have a question, need a break, or need help. The panel sends teacher requests to a separate teacher device over Bluetooth Low Energy (BLE). That device stores the week as a CSV file, which can be imported into the website.

The current student device is an Elecrow CrowPanel 5.0 inch display. The ESP32-S3 is already on that board. `GEEN1400_Teacher` is the teacher device. `GEEN1400_Prototype.ino` is the earlier Arduino Uno version, written for a 16x2 character LCD and the old phone app.

## What the student sees

On first startup, the student types a name and saves it. The greeting previews **Hello, [name]** as they type. The saved name is retained across restarts and included with teacher requests. Give each panel its own name so the teacher can tell students apart.

The main menu has three options:

- **I have a question** opens repeat-question prompts, classroom suggestions, and a custom-message editor.
- **I need a break** sends a break request to the teacher.
- **I need help** opens material-help requests, emotion-based support, and a request to talk with the teacher in private.

Teacher-directed options have an explicit **Send** button. Question prompts let the student fill in a topic, and custom-message editors allow a message of their choosing. Suggestions and coping steps are shown locally and are not sent. The Uncomfortable screen's notes stay on the panel and are not sent. **Review IEP** is not included.

Personal-help choices include Distracted, Worried, Upset, Frustrated, and Uncomfortable. Box breathing shows four steps for one complete cycle and asks the student to repeat steps 1–4. Positive self-talk guides the student to notice a thought, consider what they would say to a friend, choose words that are true, fair, and kind, and try a small next step. The COPE option is a neutral placeholder until school-approved steps are provided.

The teacher device logs requests. **Yes** and **No** answer the newest teacher request, and the answer returns to the panel over BLE. **Back** returns to the previous screen.

## Settings

A gear button in the top right of the main menu and submenu screens opens settings.

Settings has three controls:

- **Student name.** **Change** opens a keyboard. **Save** keeps the name, and **Back** leaves it as it was. **Done** then shows the greeting again so the new name can be checked. The first letter of each word is capitalized. The name can be up to 16 characters.
- **Brightness.** The screen can be set from 20% to 100%, in steps of 10. Use **−**, **+**, or tap the bar. It does not turn all the way off.
- **Age group.** Four looks are listed: **K**, **1 - 2**, **3 - 4**, and **5**. The selection is saved. In the new menus it currently changes text sizing only; it no longer filters the former feelings-response lists.

The name, brightness, and grade choice are stored on the board and come back after a restart. `Preferences.h` is part of the ESP32 board package. Nothing extra needs to be installed for that.

## Teacher device

Open `GEEN1400_Teacher/GEEN1400_Teacher.ino`.

Board settings:

- Board: XIAO_ESP32S3
- PSRAM: OPI PSRAM
- USB CDC On Boot: Enabled
- USB Mode: USB-OTG (TinyUSB)
- Partition Scheme: Default with spiffs

The screen is a Waveshare 2.4 inch LCD, 240 by 320, SPI. The wiring is listed at the top of the sketch. The Sense board's microSD pins are left alone. Three buttons connect to ground: **Yes**, **No**, and **Next**.

The screen opens on the oldest student. **Next** moves to the following one. **Yes** and **No** answer the student on the screen, and that answer shows on the student panel. Green and orange selections are both stored.

The devices use BLE. They do not join the school Wi-Fi, and they do not use a phone.

**Privacy note:** this prototype does not authenticate or encrypt BLE messages. Do not send real student names, feelings, or other sensitive information until BLE pairing/security has been implemented and reviewed for the school environment.

### Time

Neither ESP32 has a battery-backed clock. The student panel sets its clock from the time the sketch was uploaded, saves that clock every minute, and sends it with each selection. After a reboot it continues from the last save. Time while a panel is unplugged is skipped. The teacher screen shows that time in US Mountain time, and it uses the same clock while it stays powered so a Yes or No can store when the reply happened. The CSV stores both moments as UTC numbers. The website shows the selection time in the computer's local time and the typical time from the request to the reply.

### Getting the week onto a computer

Plug the teacher device's USB-C port into the computer. A drive appears with `CLEAR.CSV`. Copy that file. Copying does not erase the week. Unplug the device when you are done.

Columns: `id`, `time_sent`, `student`, `feeling`, `choice`, `kind`, `teacher_response`, `time_answered`. `time_sent` is seconds since 1970 UTC. `kind` is `ask` or `try`. `teacher_response` is empty until the teacher presses Yes or No, and then a later line for the same `id` says `yes` or `no`. `time_answered` on that later line is when the button was pressed. The website uses it for the typical reply time.

### Erasing the week

Delete `CLEAR.CSV` on the computer, then unplug the teacher device. It starts a new file that has only the column names.

## Website

Open `website/index.html` in a browser. Sample classroom weeks are already on the page, so the graphs and the trend note can be shown before a device is plugged in. Choose `CLEAR.CSV` to add a real week beside those samples. The page names that week from the times in the file and keeps earlier imported weeks in that browser. A short note at the top says what stands out, including how the newest week compares with the one before it and how long a typical reply took. An after-class paragraph describes the class pattern from those totals. It stays on this computer and does not name a student. The teacher does not type the rows or name the week.

The saved weeks live in that browser on that computer. Another computer does not have them until the same files are opened there. Removing a week on the site only removes it from the browser.

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

`Wire.h`, `Preferences.h`, and the Bluetooth and USB pieces come with the ESP32 board package. Install **esp32 by Espressif Systems** from the Boards Manager. The teacher sketch also uses LovyanGFX for the 2.4 inch screen. `CLEAR_Link.h` is copied in both sketch folders. If that message changes, change both copies the same way.

Program the board through the USB-C port on the display. The CrowPanel uses a CH340 USB chip. If no COM port appears, install the WCH CH340 driver and use a USB cable that carries data, not a charge-only cable. In Device Manager the port should show as **USB-SERIAL CH340 (COMx)**. If upload stays on “Connecting…”, hold **BOOT**, tap **RST**, and release **BOOT**.

## Uno prototype

`GEEN1400_Prototype.ino` targets an Arduino Uno, a 16x2 HD44780 LCD, a button on pin 13, and an HM-10 Bluetooth module for RemoteXY. That sketch still uses the old phone app. The CrowPanel and the teacher device do not.
