# GEEN1400 CLEAR Bridge

CLEAR is a classroom tool for grades K–5. A student can ask for help, try a short coping step, or draw on the touchscreen. The panel tells a separate teacher device by Bluetooth. That device stores the week and drawings in its own flash. Plugging it into a computer shows the week as a CSV file and saved drawings as SVG files, which can be imported into the website.

The current student device is an Elecrow CrowPanel 5.0 inch display. The ESP32-S3 is already on that board. `GEEN1400_Teacher` is the teacher device. `GEEN1400_Prototype.ino` is the earlier Arduino Uno version, written for a 16x2 character LCD and the old phone app.

## What the student sees

The panel opens with **Hello** and the student name, then **What do you need?** The first time it is programmed, the greeting is “Hello, Connor.” A teacher can change that name in settings. Give each panel its own name. The teacher device uses that name to tell students apart.

- I have a question
- I need a break
- I need help
- Drawing Board

**I need help** opens help with the work, personal help, or talking in private. Personal help opens Distracted, Worried, Upset, Frustrated, and Uncomfortable. Each of those has a custom message.

**Drawing Board** offers graph paper with a pencil, a whiteboard with a black marker, and a chalkboard with orange, white, blue, or green chalk. The matching utensil follows the touch point. Strokes save to the panel when the student lifts a finger and return after a restart. If panel storage fills, a two-minute warning appears before the oldest panel-local drawing is removed; copies already sent to the teacher are not affected. **Save** sends an SVG copy to the teacher device, **Reset** clears only the panel's current board, and **New** saves a teacher copy before starting a blank panel board. Previously sent drawings are kept when a student resets or starts a new board.

- **Green / Try this** is something the student can do on the panel, such as box breathing, positive self-talk, or the COPE steps. The teacher is told. Box breathing sends when it is opened. **Begin** then counts 4, 3, 2, 1 through breathe in, hold, breathe out, and hold.
- **Orange / Ask** is a request, such as a break, a walk, a repeated question, or a typed message. A typed message is sent when the student presses **Send**. The teacher can answer yes or no. That answer shows along the bottom of the student panel.

Opening a list does not send a notice. The bottom of the student screen says **Sent to the teacher** after a real choice. An answer changes that line to **Teacher said yes** or **Teacher said no**. If the student picks something else before the answer arrives, the panel shows the new selection. The answer is still stored in the week file. A typed message is shortened to 39 characters, which is what the teacher note can hold.

**Back** returns to the previous list. On those screens it sits just left of the settings gear.

## Settings

A gear button in the top right opens settings. It is on the greeting, each choice list, and the chosen response. The greeting only stays up for about two seconds, so the reliable place to tap the gear is a choice list.

Settings has three controls:

- **Student name.** **Change** opens a keyboard. **Save** keeps the name, and **Back** leaves it as it was. **Done** then shows the greeting again so the new name can be checked. The first letter of each word is capitalized. The name can be up to 16 characters.
- **Brightness.** The screen can be set from 20% to 100%, in steps of 10. Use **−**, **+**, or tap the bar. It does not turn all the way off.
- **Age group.** Four looks are listed: **K**, **1 - 2**, **3 - 4**, and **5**. The green row is the one students see, marked **In use**. K uses the biggest letters, a short word, and a small picture on each button. Grades 1-2 use big letters and the same pictures, with the full words. The three choices on the first page fill the space under the title. Grade 5 uses smaller type. Every grade sees the same choices. The teacher still receives the full words. The choice is saved and comes back after a restart.

The name, brightness, and grade choice are stored on the board and come back after a restart. `Preferences.h` is part of the ESP32 board package. Nothing extra needs to be installed for that.

## Teacher device

Open `GEEN1400_Teacher/GEEN1400_Teacher.ino`.

Board settings:

- Board: XIAO_ESP32S3
- PSRAM: OPI PSRAM
- USB CDC On Boot: Enabled
- USB Mode: USB-OTG (TinyUSB)
- Partition Scheme: Default with spiffs

The screen is a Waveshare 2.4 inch LCD, 240 by 320, SPI. The wiring is listed at the top of the sketch. Three buttons connect to ground: **Yes**, **No**, and **Next**. The week log and received drawings are stored in the teacher ESP32's SPIFFS flash.

The screen opens on the oldest student. **Next** moves to the following one. **Yes** and **No** answer the student on the screen, and that answer shows on the student panel. Green and orange selections are both stored.

The devices use Bluetooth. They do not join the school Wi-Fi, and they do not use a phone.

### Time

Neither ESP32 has a battery-backed clock. The student panel sets its clock from the time the sketch was uploaded, saves that clock every minute, and sends it with each selection. After a reboot it continues from the last save. Time while a panel is unplugged is skipped. The teacher screen shows that time in US Mountain time, and it uses the same clock while it stays powered so a Yes or No can store when the reply happened. The CSV stores both moments as UTC numbers. The website shows the selection time in the computer's local time and the typical time from the request to the reply.

### Getting the week onto a computer

Plug the teacher device's USB-C port into the computer. A drive appears with `CLEAR.CSV` and any drawings saved from student panels as `DRAWxxxx.SVG` files. Copy the CSV and the SVGs you want to keep. Copying does not erase them. Unplug the device when you are done.

Columns: `id`, `time_sent`, `student`, `feeling`, `choice`, `kind`, `teacher_response`, `time_answered`. `time_sent` is seconds since 1970 UTC. `kind` is `ask` or `try`. `teacher_response` is empty until the teacher presses Yes or No, and then a later line for the same `id` says `yes` or `no`. `time_answered` on that later line is when the button was pressed. The website uses it for the typical reply time.

### Erasing the week

Delete `CLEAR.CSV` on the computer, then unplug the teacher device. It starts a new week file with only the column names. Drawing SVGs are separate files and remain on the device.

## Website

Open `website/index.html` in a browser. Sample classroom weeks are already on the page, so the graphs and the trend note can be shown before a device is plugged in. Choose the `DRAWxxxx.SVG` files first, then choose `CLEAR.CSV` to add a real week beside those samples and keep the drawings with it. The page names that week from the times in the file and keeps earlier imported weeks in that browser. The **Student drawings** gallery lets the teacher filter drawings by student. A short note at the top says what stands out, including how the newest week compares with the one before it and how long a typical reply took. An after-class paragraph describes the class pattern from those totals. It stays on this computer and does not name a student. The teacher does not type the rows or name the week.

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

`Wire.h`, `Preferences.h`, SPIFFS, and the Bluetooth and USB pieces come with the ESP32 board package. Install **esp32 by Espressif Systems** from the Boards Manager. The teacher sketch also uses LovyanGFX for the 2.4 inch screen. `CLEAR_Link.h` is copied in both sketch folders. If that message changes, change both copies the same way.

Program the board through the USB-C port on the display. The CrowPanel uses a CH340 USB chip. If no COM port appears, install the WCH CH340 driver and use a USB cable that carries data, not a charge-only cable. In Device Manager the port should show as **USB-SERIAL CH340 (COMx)**. If upload stays on “Connecting…”, hold **BOOT**, tap **RST**, and release **BOOT**.

## Uno prototype

`GEEN1400_Prototype.ino` targets an Arduino Uno, a 16x2 HD44780 LCD, a button on pin 13, and an HM-10 Bluetooth module for RemoteXY. That sketch still uses the old phone app. The CrowPanel and the teacher device do not.
