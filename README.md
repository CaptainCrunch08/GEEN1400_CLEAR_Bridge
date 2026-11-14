# GEEN1400 CLEAR Bridge

CLEAR is a classroom tool for grades K–5. A student can ask for help, try a short coping step, or draw on the touchscreen. The panel tells a separate teacher device by Bluetooth when a student asks for help or tries a coping step. Drawings stay on the student panel. The teacher device stores the week in its own flash. The CLEAR website reads that week over USB after the teacher device accepts the class PIN and teacher code.

The current student device is an Elecrow CrowPanel 5.0 inch display. The ESP32-S3 is already on that board. `GEEN1400_Teacher` is the teacher device. `GEEN1400_Prototype.ino` is the earlier Arduino Uno version, written for a 16x2 character LCD and the old phone app.

## What the student sees

The panel opens on a class PIN screen. Use the same four digits chosen on the teacher device. For testing, **1234** opens that screen on the panel and the teacher device. It leaves the saved class PIN alone. A locked week file still opens with the real class PIN. The teacher code is still required on the teacher device and the website. The first time a panel is programmed, the greeting is “Hello, Connor” after that PIN is entered. A teacher can change the name in settings. Give each panel its own name. The teacher device uses that name to tell students apart. A battery mark in the top right is on every student screen. The same mark is on the teacher screen. A USB cable, including a USB battery pack, only supplies power. It does not send a charge level, and neither board wires the cell to a pin the chip can read, so the mark says USB. Five minutes with no touch turns the screen off. The next tap asks for the class PIN, and a correct PIN opens the feelings thermometer. The saved name is locked with the class PIN. If the teacher has not allowed this panel, the bottom line says it is waiting.

After the greeting, the first screen is a feelings thermometer. The student slides it to show how much energy they are using. From the bottom, the zones are blue, green, yellow, orange, and red. The tube is rounded, with a blue bulb and no splash at the top. **This is me** opens a light page that asks what would help right now. **Feel Better** lists breathing and See, hear, feel. **Ask for Help** lists the accommodations that are on. **Draw or Write** opens a chalkboard or a notebook, and it can be turned off in settings. **Tools** lists **Race Bridger**, **Schedule**, and **First, then**. Those two share one five-minute timer. It starts when either one opens and keeps going if the student switches between them. The time shows on the screen. At the end, the panel returns to the help page. The drawing stays on the board. Yellow, orange, and red tell the teacher. Blue and green stay on the panel.

**Help** sits on the bottom left of every major student screen and tells the teacher. **Calm screen**, under Student tools, turns the cards gray, hides pictures and Bridger, and leaves breathing as a number and a word. **Strategy order** on that same page moves the coping tools up so the most useful one is first. **Feel Better** keeps **See, hear, feel** and breathing. **Tools** keeps **First, then**, **Schedule**, and **Race Bridger**. He walks to a pond for five minutes. The student can try to finish a small job first. If Bridger arrives first, he waits. Nothing is lost, and the teacher is not told. See, hear, feel asks for 3 things to see, then 2 things to touch, then 1 sound. Each thing to see has a look, such as green or pointy. The same tools are offered in every grade. Kindergarten through grade 2 blows out a candle. Grades 3 through 5 follow a dot that traces a box, one side for each count of 4. The number counts 1 to 4 breathing in, 4 to 1 on the hold, 1 to 4 breathing out, and 4 to 1 on the last hold. **Begin** starts the whole sequence. It plays three times, then **Did that help?** offers **Yes**, **Try again**, and **Not yet**. **Yes** and **Not yet** are saved in the week when that check-in was already shared with the teacher. Blue and green stay on the panel. **Try again** is not counted. **I need my teacher** sends that request. **Try again** uses the redo mark. **Not yet** uses a soft x. The gear asks for a settings password before name, grade, or tools can change. The first time, that password is chosen and entered twice. It is separate from the class PIN, so a student who can open the panel still cannot change settings. **Student tools** turns each tool on or off. **Words** on First, then puts First and Then side by side. The box under them is school activities, and the green side is the one a tap fills. Custom types a word that is not in the box. **Words** on Schedule shows four blocks with arrows between them. The green block is the one a tap fills, from the same school activities, or Custom. On the student schedule the current block is green, and **Next activity** moves that highlight to the next block. **Accommodations** in settings turns on the requests this student may make. The ones that are on appear under **Ask for Help**. Tapping one asks the teacher, who can accept, decline, or say in a minute. Those words stay on the panel. A tool that is off is not offered after **This is me**.

**Drawing Board** offers graph paper with a pencil, a whiteboard with a black marker, and a chalkboard with orange, white, blue, green, yellow, pink, red, or purple chalk. Picking a color changes the chalk in hand. Strokes already on the board keep their color. The matching utensil follows the touch point. Strokes save on the student panel when the student lifts a finger, and again when they tap **Save**. They come back after a restart. They are not sent to the teacher device or the website. If panel storage fills, a two-minute warning appears before the oldest drawing on that panel is removed. **Reset** and **New** clear the board on the panel. **Back** leaves the board. When readable text is already stored beside a drawing, **Read** switches from the strokes to that text; tap its footer to move through pages, and **Draw** returns to the strokes.

- **Green / Try this** is something the student can do on the panel, such as breathing, positive self-talk, or the COPE steps. The teacher is told. Breathing sends when it is opened. **Begin** then counts 4, 3, 2, 1. Grades K through 2 see a candle: the flame grows on the breath in, then leans away while they blow. Grades 3 through 5 keep the box, with breathe in, hold, breathe out, and hold. The teacher note says Candle blowing or Box breathing to match that grade.
- **Orange / Ask** is a request, such as a break, a walk, a repeated question, or a typed message. A typed message is sent when the student presses **Send**. The teacher can answer yes or no. An accommodation request can also be answered in a minute. That answer shows along the bottom of the student panel.

Opening a list does not send a notice. The bottom of the student screen says **Sending** until the teacher device replies that it has the note. It does not say the note was sent on the radio write alone. An answer changes that line to Pending, Seen, or the teacher’s name plus yes or no, and the request they answered. Each panel keeps its own event number across a reboot, and the website attaches an answer to the student who sent that number. Neither board has a clock battery. The last time is saved, and when the teacher device has Wi-Fi configured it sets the clock from the network and the student panel catches up. The teacher device keeps the newest part of the week file and removes older rows. If the student picks something else before the answer arrives, the panel shows the new selection. The answer is still stored in the week file. A typed message is shortened to 39 characters, which is what the teacher note can hold.

**Back** returns to the previous list. On those screens it sits just left of the settings gear.

## Settings

A gear button in the top right asks for the settings password, then opens settings. It is on the greeting, the thermometer, and the zone screen. The greeting only stays up for about two seconds, so the reliable place to tap the gear is the thermometer. **Back** on the password screen returns to the student page without opening settings.

**Look** on the settings page switches the whole panel between light and dark. Light is a white page. Dark keeps the same screens, with deeper greens, purples, and oranges so they stay quiet. **Teacher** on that page is the name used in place of the word Teacher, including “said yes to” the request the student made. The handshake heart to the left of CLEAR Bridge is green. The settings gear is a soft gray icon.

The CLEAR website can change those same panel settings. On the **Student panel** section, connect with **Bluetooth**. The page has to be opened from a local address rather than as a file. Bluetooth looks for a panel named CLEAR panel. Leave **USB CDC On Boot** disabled on the student board. That switch uses GPIO 19 and 20, which are the touch wires, so the picture stays up and taps stop. The website asks for the settings password. That password is not the class PIN, and **1234** does not open settings unless a teacher chose it as the settings password. The password is not saved in the browser. Five wrong tries make the panel wait a minute. If the panel has no settings password yet, set one with the gear before connecting.

Settings has these controls:

- **Student name.** **Change** opens a keyboard. **Save** keeps the name, and **Back** leaves it as it was. **Done** then shows the greeting again so the new name can be checked. The first letter of each word is capitalized. The name can be up to 16 characters.
- **Brightness.** The screen can be set from 20% to 100%, in steps of 10. Use **−**, **+**, or tap the bar. It does not turn all the way off.
- **Bridger the beaver.** On puts a small square beaver in the bottom-right corner. His bubble changes with what the student just did. Off hides him.
- **Draw or write.** On shows that card after the thermometer. Off hides it. The choice is saved.
- **Age group.** Four looks are listed: **K**, **1 - 2**, **3 - 4**, and **5**. The green row is the one students see, marked **In use**. K uses the biggest letters, a short word, and a small picture on each button. Grades 1-2 use big letters and the same pictures, with the full words. The three choices on the first page fill the space under the title. Grade 5 uses smaller type. Every grade sees the same choices. The teacher still receives the full words. The choice is saved and comes back after a restart.

The name, brightness, Bridger, and grade choice are stored on the board and come back after a restart. `Preferences.h` is part of the ESP32 board package. Nothing extra needs to be installed for that.

## Teacher device

Open `GEEN1400_Teacher/GEEN1400_Teacher.ino`.

Board settings:

- Board: ESP32 Dev Module
- Partition Scheme: No OTA (2MB APP/2MB SPIFFS)

The screen is a 2.4 inch ILI9341, 240 by 320. The picture uses SPI. Touch is the capacitive chip on I2C, address 0x15. The wiring is listed at the top of the sketch. The picture is turned right side up, and a tap lands on the button under the finger. **Yes**, **No**, and **Next** are buttons along the bottom of the glass. A gear in the top right opens **Look**. **Brightness** steps from 20% to 100%, and 100% stays below full backlight power because full power on this panel is harsh. **Light** and **Dark** change the page colors. **Student settings** opens toggles for drawing, calm screen, Bridger, and the student tools. A change is sent over Bluetooth and the student panel redraws the page it is on. A yellow, orange, or red thermometer notice has one button, **I'll be there soon**. Other requests still use **Yes**, **No**, and **Next**. These choices are saved and come back after a restart. The same gear is on the PIN screen, so the light can be turned down before the student list opens. A button from GPIO22 to ground blanks the glass and turns the backlight off without cutting power or Bluetooth. Press it again to bring the picture back. A buzzer on GPIO21 sounds for one second when a student choice arrives, two short buzzes for an accommodation, and two short buzzes then a longer one for **Help** or **I need my teacher**. The week log is stored in the teacher ESP32's SPIFFS flash. Student drawings are not stored there. This board does not present that flash as a USB drive.

The first time it starts, it asks for a class PIN and then a teacher code. Enter each one twice. Later it asks for the class PIN and then the teacher code before it shows a student. Five minutes with no touch hides the message until the class PIN is entered again. Tap the empty waiting screen to review panels you have already allowed, and remove one if a family asks you to stop keeping that child.

After the codes, the screen opens on the oldest student. **Next** moves to the following one. **Yes** and **No** answer the student on the screen, and that answer shows on the student panel. A request is **Pending** when the teacher device has it but it is not on the glass yet. It becomes **Seen** when that request is the one on the teacher screen. **Yes**, **No**, or **In a minute** means it is answered. The student panel shows the same three steps.

A student panel that is not on the allowed list is held aside. **Allow** saves that panel’s name and the message it just sent. **Not now** goes back to the queue and leaves the panel waiting. The message is not shown and is not written into the week until you allow it. Bluetooth uses LE Secure Connections. The class PIN is the passkey, and a paired panel is remembered.

Student notices use Bluetooth Low Energy. Drawings stay on the student panel and are not sent to the teacher device.

### Time

Neither ESP32 has a battery-backed clock. The student panel sets its clock from the time the sketch was uploaded, saves that clock every minute, and sends it with each selection. After a reboot it continues from the last save. Time while a panel is unplugged is skipped. The teacher screen shows that time in US Mountain time, and it uses the same clock while it stays powered so a Yes or No can store when the reply happened. The CSV stores both moments as UTC numbers. The website shows the selection time in the computer's local time and the typical time from the request to the reply.

### Getting the week onto a computer

Enter the class PIN and the teacher code on the teacher device first. This 2.4 inch board keeps the week in its own flash and does not show it as a USB drive. The week file is locked with the class PIN. The website reads that file over the USB port after this device accepts the same class PIN and teacher code. Student drawings are not part of that transfer.

Columns: `id`, `time_sent`, `student`, `feeling`, `choice`, `kind`, `teacher_response`, `time_answered`. `time_sent` is seconds since 1970 UTC. `kind` is `ask` or `try`. `teacher_response` is empty on the first line. A later line for the same `id` says `pending` when the device received it, `seen` when it was on the teacher screen, and `yes`, `no`, or `minute` when the teacher answered. `time_answered` on that later line is when that step happened. The website uses the answer line for the typical reply time.

### Erasing the week

This board keeps the week in its own flash. Deleting a copy of `CLEAR.CSV` on the computer does not erase the device.

## Website

Open `website/index.html` in Chrome or Edge from a local address, such as the preview server. The page starts with a privacy notice. Plug in the teacher device and choose **Connect teacher device**. The page then asks for the class PIN and teacher code saved on that device. It does not keep those codes or the week after the page is locked or closed. **View sample class** shows example weeks without a device. A locked week file still opens with the class PIN. The page names that week from the times in the file. The top of the page is a weekly overview: how often students used the panel, how many of those moments were teacher requests, how long a typical reply took, and one class-level note about what changed from the week before. That note does not name a student. The download icon saves that overview as a text file on this computer. The charts below show the days, the requests to plan for, and the feelings worth a closer look. **Did it help?** groups the Yes and Not yet answers by strategy. A higher Yes count is not a grade. **Choose a student** opens that student’s strategies, accommodation requests, reply times, week-by-week counts, and the same Yes and Not yet answers. Those counts are not labeled better or worse. More requests can mean the student is asking for help more clearly. The teacher does not type the rows or name the week. Student drawings are not loaded onto this page.

Names and messages hide after 5 minutes with no mouse or keyboard activity. The class PIN shows them again. **Lock** covers the page. Another computer does not have the week until the teacher device is connected there.

## CrowPanel sketch

Open `GEEN1400_Student/GEEN1400_CLEAR_Bridge/GEEN1400_CLEAR_Bridge.ino` in the Arduino IDE.

Board settings:

- Board: ESP32S3 Dev Module
- PSRAM: OPI PSRAM
- Flash Size: 4MB (32Mb)
- Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS). Drawings are saved in that SPIFFS area. The first start prepares it. A later start keeps the drawings.
- Flash Mode: QIO 80MHz
- CPU Frequency: 240MHz (WiFi/BT)

Libraries, from the Arduino Library Manager:

- LovyanGFX 1.2 or later. The sketch includes the RGB panel headers directly, because that version does not expose them from `LovyanGFX.hpp`.

`Wire.h`, `Preferences.h`, SPIFFS, and the Bluetooth and USB pieces come with the ESP32 board package. Install **esp32 by Espressif Systems** from the Boards Manager. The teacher sketch also uses LovyanGFX for the 2.4 inch screen. `CLEAR_Link.h` is copied in both sketch folders. If that message changes, change both copies the same way.

Program the board through the USB-C port on the display. The CrowPanel uses a CH340 USB chip. If no COM port appears, install the WCH CH340 driver and use a USB cable that carries data, not a charge-only cable. In Device Manager the port should show as **USB-SERIAL CH340 (COMx)**. If upload stays on “Connecting…”, hold **BOOT**, tap **RST**, and release **BOOT**.

## Uno prototype

`GEEN1400_Prototype.ino` targets an Arduino Uno, a 16x2 HD44780 LCD, a button on pin 13, and an HM-10 Bluetooth module for RemoteXY. That sketch still uses the old phone app. The CrowPanel and the teacher device do not.
