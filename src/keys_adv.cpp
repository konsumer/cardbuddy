// The Cardputer ADV's keyboard (a TCA8418 on I2C), read by polling: M5Cardputer's own reader takes one event per
// interrupt, and one interrupt missed leaves a key held for good (its release never read), so an arrow repeated by
// itself. This takes every event waiting, each time it's asked
#include <M5Cardputer.h>

#include <algorithm>
#include <memory>

#include "utility/Adafruit_TCA8418/Adafruit_TCA8418.h"
#include "utility/Keyboard/KeyboardReader/KeyboardReader.h"

#define ADV_KEY_INT 11  // (the library's interrupt pin: its handler is taken off)

struct PolledTCA8418 : KeyboardReader {
  Adafruit_TCA8418 tca;
  bool up = false;
  void begin() override {
    up = tca.begin() && tca.matrix(7, 8);
    tca.flush();
  }
  void update() override {
    for (int n = 0; up && n < 16 && tca.available(); n++) {
      uint8_t e = tca.getEvent();
      bool down = e & 0x80;
      uint8_t k = (e & 0x7F) - 1, row = k / 10, col = k % 10;
      // as M5Cardputer's TCA8418 reader lays them out (the original Cardputer's matrix)
      Point2D_t p;
      p.x = row * 2 + (col > 3);
      p.y = col % 4;
      auto it = std::find(_key_list.begin(), _key_list.end(), p);
      if (down && it == _key_list.end()) {
        _key_list.push_back(p);
      } else if (!down && it != _key_list.end()) {
        _key_list.erase(it);
      }
    }
  }
};

void keysBegin() {
  if (M5.getBoard() != m5::board_t::board_M5CardputerADV) {
    return;  // (the original Cardputer's keys are a matrix on GPIOs: polled already)
  }
  detachInterrupt(digitalPinToInterrupt(ADV_KEY_INT));  // (its handler points at the reader replaced here)
  M5Cardputer.Keyboard.begin(std::unique_ptr<KeyboardReader>(new PolledTCA8418()));
}
