// cardbuddy: fm1buddy's core (fm1core) and its ESP32-S3 side (fm1s3) on the M5 Cardputer, with a keyboard UI
#include <M5Cardputer.h>

#include "board.h"
#include "hal.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);  // (with the keyboard)
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
  board::loop();  // (the USB port: a computer on it, or the host)
  board::bleLoop();
  ui::loop();
  delay(2);
}
