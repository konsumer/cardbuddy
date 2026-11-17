// cardbuddy: fm1buddy's core (fm1core) and its ESP32-S3 side (fm1s3) on the M5 Cardputer, with a keyboard UI
#include <M5Cardputer.h>

#include "board.h"
#include "hal.h"
#include "ui.h"

void keysBegin();

// the serial console, for testing (tools/cb.py): "key NAME ..." presses keys (enter, esc, up, down, left, right, tab,
// del, space, or a character), "shot" sends the screen (SHOT w h, then w*h RGB565 pixels, little-endian), "rm PATH" removes a file
// on the SD card
static void console() {
  static char line[160];
  static uint8_t n;
  while (!hal::usbHost() && Serial.available() > 0) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (n < sizeof line - 1) {
        line[n++] = c;
      }
      continue;
    }
    line[n] = 0;
    n = 0;
    if (!strcmp(line, "shot")) {
      Serial.printf("SHOT %d %d\n", SCREEN_W, SCREEN_H);
      ui::shot([](const uint16_t* px, int k) { Serial.write((const uint8_t*)px, k * 2); });
      Serial.flush();
    } else if (!strncmp(line, "key ", 4)) {
      for (char* t = strtok(line + 4, " "); t; t = strtok(0, " ")) {
        ui::Key k = {};
        if (!strcmp(t, "enter")) {
          k.enter = true;
        } else if (!strcmp(t, "esc")) {
          k.back = true;
        } else if (!strcmp(t, "up")) {
          k.up = true;
        } else if (!strcmp(t, "down")) {
          k.down = true;
        } else if (!strcmp(t, "left")) {
          k.left = true;
        } else if (!strcmp(t, "right")) {
          k.right = true;
        } else if (!strcmp(t, "tab")) {
          k.tab = true;
        } else if (!strcmp(t, "del")) {
          k.del = true;
        } else if (!strcmp(t, "space")) {
          k.ch = ' ';
        } else {
          k.ch = t[0];
        }
        ui::inject(k);
      }
      Serial.println("OK");
    } else if (!strncmp(line, "rm ", 3)) {  // (a file on the SD card: rm /fm1buddy/hub.etag, say)
      Serial.println(remove(("/sdcard" + std::string(line + 3)).c_str()) ? "can't" : "OK");
    } else if (!strcmp(line, "ram")) {
      hal::logRam("asked");
    }
  }
}

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);  // (with the keyboard)
  keysBegin();                   // (the ADV's, polled: keys_adv.cpp)
  board::begin();
  // in recovery mode the USB port is the FM-1's at once: the recover page's job sends its key on it (key_s3.cpp)
  if (hal::recoveryMode()) {
    board::usbBegin();
  }
  ui::begin();
  hal::logRam("up");
  hal::log("screen: %dx%d, rotation %d", (int)M5Cardputer.Display.width(), (int)M5Cardputer.Display.height(),
           (int)M5Cardputer.Display.getRotation());
}

void loop() {
  M5Cardputer.update();
  console();
  board::loop();  // (the USB port: a computer on it, or the host)
  board::bleLoop();
  ui::loop();
  delay(2);
}
