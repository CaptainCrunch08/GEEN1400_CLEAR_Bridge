# GEEN1400 CLEAR Bridge

CLEAR is a classroom support tool for grades K–5. A student enters their name, then chooses whether they have a question, need a break, or need help. The panel can send a request to a separate teacher device over Bluetooth Low Energy (BLE). That device stores requests on a microSD card. At the end of the week the file is copied onto a computer and imported into the website.

The current student device is an Elecrow CrowPanel 5.0 inch display. The ESP32-S3 is already on that board. `GEEN1400_Teacher` is the teacher device. `GEEN1400_Prototype.ino` is the earlier Arduino Uno version, written for a 16x2 character LCD and the old phone app.

## What the student sees

On startup, the student types a name, previews **Hello, [name]**, and presses **Save name**. The name is saved in flash and included with teacher requests. The main menu has four options:

- **I have a question** opens repeat-question prompts, classroom suggestions, and a custom-message editor.
- **I need a break** sends a break request to the teacher.
- **I need help** opens material-help requests, emotion-based support, and a private-conversation request.
- **Drawing Board** opens graph paper, a whiteboard, or a chalkboard. A pencil or black marker follows the touch point; the chalkboard offers orange, white, blue, and green chalk. Completed strokes save to the panel, so the latest graph, whiteboard, and chalkboard drawings return after a restart. **Save** sends a drawing to the teacher device, **Reset** clears the current panel copy, and **New** sends the current drawing before starting a blank board of the same type.

Teacher-directed options have a **Send** button. Custom question prompts let the student fill in the missing topic before sending. Suggestions and coping steps are shown locally and are not sent. The Uncomfortable screen's notes stay on the panel and are not sent. **Review IEP** is not included.

The teacher device logs sent requests and can answer them Yes or No. The answer travels back to the panel over ESP-NOW and appears in the status strip. **Back** returns to the prior menu.

## Settings

A gear button in the top right of the main and submenu screens opens settings.

Settings retains three controls:

- **Student name.** **Change** opens a keyboard. **Save** keeps the name, and **Back** leaves it as it was. The first letter of each word is capitalized. The name can be up to 16 characters.
- **Brightness.** The screen can be set from 20% to 100%, in steps of 10. Use **−**, **+**, or tap the bar. It does not turn all the way off.
- **Age group.** Four looks are listed: **K**, **1 - 2**, **3 - 4**, and **5**. The selection is saved. In the new menus it currently changes text sizing only; it no longer filters the former feelings-response lists.

The name, brightness, and grade choice are stored on the board and come back after a restart. `Preferences.h` is part of the ESP32 board package. Nothing extra needs to be installed for that.

The three latest panel drawings use the SPIFFS partition. Strokes save when the student lifts a finger. If the panel cannot save a board locally or prepare its teacher-card export because of a storage issue, it shows a two-minute warning and then removes the oldest panel-local saved board to make room and retries the local save. A drawing already sent to `/student-drawings` on the teacher card is not removed by this cleanup. Each board is limited to 2,400 line segments so one drawing cannot consume the whole partition.

## Teacher device

Open `GEEN1400_Teacher/GEEN1400_Teacher.ino`. Pick the ESP32 board that matches the mini you bought.

The sketch is a framework. BLE request reception, the microSD log, and the buttons work. The 2.5 inch screen is not chosen yet, so `displayBegin`, `displayShow`, and `displayStatus` print to the Serial Monitor at 115200. Those three functions are the place to add the real screen. The pin numbers at the top of the sketch have to match the wires.

The teacher device advertises the CLEAR BLE service; the student panel scans for that service when the student sends a request. They do not join school Wi-Fi or use a phone. ESP-NOW on channel 1 is retained for teacher Yes/No replies. Several student panels can send requests to one teacher device.

**Privacy note:** this prototype currently does not authenticate or encrypt BLE messages. Do not use it to transmit real student names, feelings, or other sensitive information until BLE pairing/security has been implemented and reviewed for the school environment.

Every BLE request received is shown and added to the log. **Yes** and **No** answer the newest request and send that answer back to the panel that made it. Another press answers the one before it.

The teacher device has no clock chip. The time column is seconds since that device was turned on. It starts over at the next power-on. The website does not need a clock. Naming the file at import is what marks the week.

### Getting the week onto a computer

The file on the card is `clear-log.csv`. Turn the teacher device off, take the microSD card out, and put it in a USB card reader. The ESP32 USB port programs the board. It does not show up as a drive. Copying the file leaves it on the card.

Columns: `id`, `seconds_on`, `student`, `feeling`, `choice`, `kind`, `teacher_response`. `kind` is `ask` or `try`. `teacher_response` is empty until the teacher presses Yes or No, and then a later line for the same `id` says `yes` or `no`.

Saved drawings arrive separately over BLE in `/student-drawings` on the teacher card. Copy the SVG files in that folder along with `clear-log.csv`. Clearing the week log does not erase drawings.

### Erasing the week

**Clear** is its own button, not Yes or No. Press **Clear**, then press **Yes** within 8 seconds. The log is replaced with a file that has only the column names, and the screen says the week was cleared. **No** cancels.

Deleting `clear-log.csv` on the computer does the same job. The next time the teacher device starts, it creates a new file with only the column names.

## Website

Open `website/index.html` in a browser. Choose `clear-log.csv`, name the week, optionally select the SVGs copied from `/student-drawings`, and save. Open **Student drawings** and choose a student to view their imported boards. Each week and its drawings stay in that browser. Trends cover feelings, students, try versus ask, teacher answers, and how many selections each week had.

The saved weeks live in that browser on that computer. Another computer does not have them until the same CSV files are imported there. Removing a week on the site only removes it from the browser. A shared server can come later.

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

`WiFi.h`, `Wire.h`, `Preferences.h`, ESP-NOW, and BLE come with the ESP32 board package. Install **esp32 by Espressif Systems** from the Boards Manager. The teacher sketch uses the SD library from that same package. It does not need LovyanGFX. `CLEAR_Link.h` is copied in both sketch folders. If the shared message format or BLE UUIDs change, change both copies the same way.

Program the board through the USB-C port on the display. The CrowPanel uses a CH340 USB chip. If no COM port appears, install the WCH CH340 driver and use a USB cable that carries data, not a charge-only cable. In Device Manager the port should show as **USB-SERIAL CH340 (COMx)**. If upload stays on “Connecting…”, hold **BOOT**, tap **RST**, and release **BOOT**.

## Uno prototype

`GEEN1400_Prototype.ino` targets an Arduino Uno, a 16x2 HD44780 LCD, a button on pin 13, and an HM-10 Bluetooth module for RemoteXY. That sketch still uses the old phone app. The CrowPanel and the teacher device do not.
