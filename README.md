# GEEN1400 CLEAR Bridge

CLEAR is a classroom tool for grades K–5. A student picks how they feel, then either tries a short coping step or asks the teacher. The panel tells a separate teacher device by radio. That device stores the week on a microSD card. At the end of the week the file is copied onto a computer and imported into the website.

The current student device is an Elecrow CrowPanel 5.0 inch display. The ESP32-S3 is already on that board. `GEEN1400_Teacher` is the teacher device. `GEEN1400_Prototype.ino` is the earlier Arduino Uno version, written for a 16x2 character LCD and the old phone app.

## What the student sees

The panel opens with a greeting, then **How do you feel?** The first time it is programmed, the greeting is “Hi, Connor.” A teacher can change that name in settings. Give each panel its own name. The teacher device uses that name to tell students apart.

- Worried
- Bored
- I don't get it
- Mad
- Uncomfortable

Each feeling opens a list of responses.

- **Green / Try this** is something the student can do alone, such as breathing, counting to 10, or a quiet looking game. The next screen gives the steps. The teacher is still told.
- **Orange / Ask teacher** is a request, such as a short walk, talking with someone, a clearer explanation, or an example. The teacher can answer yes or no. That answer shows along the bottom of the student panel.

The bottom of the student screen says **Sent to the teacher** after a tap. An orange request then changes to **Teacher said yes** or **Teacher said no**. A green choice stays on “Sent to the teacher,” because it is not waiting for an answer. If the student picks something else before the answer arrives, the panel shows the new selection. The answer is still stored in the week file.

**Back** returns to the previous list. On those screens it sits just left of the settings gear.

## Settings

A gear button in the top right opens settings. It is on the greeting, the feelings list, each response list, and the chosen response. The greeting only stays up for about two seconds, so the reliable place to tap the gear is the feelings list.

Settings has three controls:

- **Student name.** **Change** opens a keyboard. **Save** keeps the name, and **Back** leaves it as it was. **Done** then shows the greeting again so the new name can be checked. The first letter of each word is capitalized. The name can be up to 16 characters.
- **Brightness.** The screen can be set from 20% to 100%, in steps of 10. Use **−**, **+**, or tap the bar. It does not turn all the way off.
- **Age group.** Four looks are listed: **K**, **1 - 2**, **3 - 4**, and **5**. The green row is the one students see, marked **In use**. K shows a face and one word for each feeling, and only the shortest responses. Grades 1-2 use large letters and the first three responses. Grades 3-4 is the full list of short sentences. Grade 5 uses smaller type and puts the first line of the instructions on each response button. The choice is saved and comes back after a restart.

The name, brightness, and grade choice are stored on the board and come back after a restart. `Preferences.h` is part of the ESP32 board package. Nothing extra needs to be installed for that.

## Teacher device

Open `GEEN1400_Teacher/GEEN1400_Teacher.ino`. Pick the ESP32 board that matches the mini you bought.

The sketch is a framework. The radio, the microSD log, and the buttons work. The 2.5 inch screen is not chosen yet, so `displayBegin`, `displayShow`, and `displayStatus` print to the Serial Monitor at 115200. Those three functions are the place to add the real screen. The pin numbers at the top of the sketch have to match the wires.

Both devices use radio channel 1. They do not join the school Wi-Fi, and they do not use a phone. Several student panels can talk to one teacher device.

Every selection is shown and added to the log. **Yes** and **No** answer the newest orange request and send that answer back. Another press answers the one before it. Green choices are logged, and they do not use Yes or No.

The teacher device has no clock chip. The time column is seconds since that device was turned on. It starts over at the next power-on. The website does not need a clock. Naming the file at import is what marks the week.

### Getting the week onto a computer

The file on the card is `clear-log.csv`. Turn the teacher device off, take the microSD card out, and put it in a USB card reader. The ESP32 USB port programs the board. It does not show up as a drive. Copying the file leaves it on the card.

Columns: `id`, `seconds_on`, `student`, `feeling`, `choice`, `kind`, `teacher_response`. `kind` is `ask` or `try`. `teacher_response` is empty until the teacher presses Yes or No, and then a later line for the same `id` says `yes` or `no`.

### Erasing the week

**Clear** is its own button, not Yes or No. Press **Clear**, then press **Yes** within 8 seconds. The log is replaced with a file that has only the column names, and the screen says the week was cleared. **No** cancels.

Deleting `clear-log.csv` on the computer does the same job. The next time the teacher device starts, it creates a new file with only the column names.

## Website

Open `website/index.html` in a browser. Choose the CSV, name the week, and save it. Each week stays in that browser. Saving another week does not replace the earlier ones unless it is given the same name. Trends cover feelings, students, try versus ask, teacher answers, and how many selections each week had.

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

`WiFi.h`, `Wire.h`, and `Preferences.h` come with the ESP32 board package. Install **esp32 by Espressif Systems** from the Boards Manager. The teacher sketch uses the SD and ESP-NOW pieces of that same package. It does not need LovyanGFX. `CLEAR_Link.h` is copied in both sketch folders. If that radio message changes, change both copies the same way.

Program the board through the USB-C port on the display. The CrowPanel uses a CH340 USB chip. If no COM port appears, install the WCH CH340 driver and use a USB cable that carries data, not a charge-only cable. In Device Manager the port should show as **USB-SERIAL CH340 (COMx)**. If upload stays on “Connecting…”, hold **BOOT**, tap **RST**, and release **BOOT**.

## Uno prototype

`GEEN1400_Prototype.ino` targets an Arduino Uno, a 16x2 HD44780 LCD, a button on pin 13, and an HM-10 Bluetooth module for RemoteXY. That sketch still uses the old phone app. The CrowPanel and the teacher device do not.
