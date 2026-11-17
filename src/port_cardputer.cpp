// fm1s3's port.h on the M5 Cardputer (ADV): the SD card on SPI (shared with the LoRa cap: GPIO5 gives the card the
// bus), the screen's light and the battery through M5Cardputer. No LED, and no self-update (M5 Launcher owns the
// app slots)
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>

#include "hal.h"
#include "port.h"

#define SD_SCK 40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS 12
#define SD_GATE 5

bool port::sdBegin() {
  pinMode(SD_GATE, OUTPUT);
  digitalWrite(SD_GATE, HIGH);
  delay(2);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, SPI, 25000000, "/sdcard")) {
    return true;
  }
  SD.end();
  delay(10);
  return SD.begin(SD_CS, SPI, 16000000, "/sdcard");
}

bool port::sdSpace(uint64_t& total, uint64_t& used) {
  total = SD.totalBytes();
  used = SD.usedBytes();
  return true;
}

uint32_t port::sdSectors() {
  return SD.numSectors();
}

uint32_t port::sdSectorSize() {
  return SD.sectorSize();
}

bool port::sdRead(uint8_t* buf, uint32_t sector) {
  return SD.readRAW(buf, sector);
}

bool port::sdWrite(const uint8_t* buf, uint32_t sector) {
  return SD.writeRAW((uint8_t*)buf, sector);
}

void port::backlight(uint8_t percent) {
  M5Cardputer.Display.setBrightness(percent * 255 / 100);
}

uint32_t port::batteryMv() {
  return M5Cardputer.Power.getBatteryVoltage();
}

void port::led(uint8_t, uint8_t, uint8_t) {}

bool port::selfUpdates() {
  return false;
}

// ---------------------------------------------------------------------------- what fm1s3 leaves to the app ---
// no touchscreen: the keyboard plays instead (MIDI_TOUCH is the keyboard's device)
int hal::fingers(Finger*, int) {
  return 0;
}

void hal::touchCal(const TouchCal*) {}

void hal::sleep() {
  // (later: deep sleep, woken by a key)
}
