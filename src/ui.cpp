#include "ui.h"

#include <M5Cardputer.h>

#include <atomic>
#include <vector>

#include "catalog.h"
#include "fm1.h"
#include "hal.h"
#include "jobs.h"
#include "midi.h"
#include "play.h"
#include "settings.h"
#include "themes.h"

// the whole screen, 4 bits a pixel: 16 KB of the internal RAM, where 16 bits would take 64 KB
static M5Canvas canvas(&M5Cardputer.Display);
static std::vector<ui::Page*> stack;
static bool dirty = true;
static std::string toastText;
static bool toastBad;
static uint32_t toastUntil, lastTick, seenFinished;

static const Theme& theme() {
  return THEMES[settings.theme < THEME_COUNT ? settings.theme : 0];
}

void ui::applyTheme() {
  const Theme& t = theme();
  uint32_t c[] = {t.bg, t.panel, t.off, t.beat, t.text, t.dim, t.accent, t.knob[0], t.knob[1], t.knob[2], t.knob[3]};
  for (int i = 0; i < (int)(sizeof c / sizeof c[0]); i++) {
    canvas.setPaletteColor(i, c[i]);
  }
  hal::backlight(settings.brightness ? settings.brightness : 80);
  dirty = true;
}

// the flash map's colors for the FM-1's own screen while its loader runs (jobs.h): the theme's
void jobs::screenColors(uint32_t rgb[SCREEN_COLORS]) {
  const Theme& t = theme();
  uint32_t c[SCREEN_COLORS] = {t.bg, t.off, t.knob[1], t.knob[2], t.accent, t.beat, t.knob[3], t.knob[0], t.text, t.dim};
  memcpy(rgb, c, sizeof c);
}

// -------------------------------------------------------------------------------------------- drawing ---
void ui::fill(int x, int y, int w, int h, uint8_t color) {
  canvas.fillRect(x, y, w, h, color);
}

void ui::text(int x, int y, const char* s, uint8_t color, int maxW) {
  canvas.setTextColor(color);
  if (maxW > 0 && canvas.textWidth(s) > maxW) {
    std::string t = s;
    while (!t.empty() && canvas.textWidth((t + "~").c_str()) > maxW) {
      t.pop_back();
    }
    t += "~";
    canvas.drawString(t.c_str(), x, y);
    return;
  }
  canvas.drawString(s, x, y);
}

void ui::textRight(int x, int y, const char* s, uint8_t color) {
  canvas.setTextColor(color);
  canvas.drawString(s, x - canvas.textWidth(s), y);
}

void ui::bar(int x, int y, int w, int h, uint32_t done, uint32_t total) {
  canvas.fillRect(x, y, w, h, OFF);
  if (total) {
    canvas.fillRect(x, y, (int)((uint64_t)w * std::min(done, total) / total), h, ACCENT);
  }
}

int ui::wrap(int x, int y, int w, const char* s, uint8_t color, int maxLines) {
  int lines = 0;
  std::string line, word;
  auto flush = [&]() {
    if (lines < maxLines) {
      text(x, y + lines * 10, line.c_str(), color, w);
    }
    lines++;
    line.clear();
  };
  for (const char* p = s;; p++) {
    if (*p && *p != ' ' && *p != '\n') {
      word += *p;
      continue;
    }
    std::string next = line.empty() ? word : line + " " + word;
    if (canvas.textWidth(next.c_str()) > w && !line.empty()) {
      flush();
      line = word;
    } else {
      line = next;
    }
    word.clear();
    if (*p == '\n') {
      flush();
    }
    if (!*p) {
      break;
    }
  }
  if (!line.empty()) {
    flush();
  }
  return std::min(lines, maxLines);
}

void ui::listRows(int count, int sel, int& scroll, int y0, int rows, void (*row)(int, int, bool, void*), void* arg) {
  if (sel < scroll) {
    scroll = sel;
  }
  if (sel >= scroll + rows) {
    scroll = sel - rows + 1;
  }
  scroll = std::max(0, std::min(scroll, std::max(0, count - rows)));
  for (int i = scroll; i < count && i < scroll + rows; i++) {
    int y = y0 + (i - scroll) * ROW;
    if (i == sel) {
      fill(0, y, SCREEN_W - 3, ROW, OFF);
      fill(0, y, 2, ROW, ACCENT);
    }
    row(i, y, i == sel, arg);
  }
  if (count > rows) {  // the scroll bar
    int h = std::max(6, (BOTTOM - y0) * rows / count);
    int at = y0 + (BOTTOM - y0 - h) * scroll / std::max(1, count - rows);
    fill(SCREEN_W - 2, y0, 2, BOTTOM - y0, PANEL);
    fill(SCREEN_W - 2, at, 2, h, DIM);
  }
}

static void drawFrame() {
  using namespace ui;
  canvas.fillScreen(BG);
  Page* p = stack.back();
  // the title bar: the page, then what's going on (a job, the battery)
  fill(0, 0, SCREEN_W, TOP - 1, PANEL);
  text(3, 3, p->title(), ACCENT, 130);
  char right[40];
  int level = M5Cardputer.Power.getBatteryLevel();
  const Fm1Id* id = fm1::known(fm1::current());
  snprintf(right, sizeof right, "%s%s%d%%", id ? (std::string(fm1::machineName()) + "  ").c_str() : "",
           hal::wifiOn() ? "wifi  " : "", level);
  textRight(SCREEN_W - 3, 3, right, DIM);
  p->draw();
  // the bottom: a job, a toast, or the page's keys
  fill(0, BOTTOM, SCREEN_W, SCREEN_H - BOTTOM, PANEL);
  if (jobs::running) {
    std::string s = jobs::status();
    if (jobs::total) {
      bar(0, BOTTOM, SCREEN_W, 2, jobs::done, jobs::total);
    }
    text(3, BOTTOM + 3, s.c_str(), TEXT, SCREEN_W - 6);
  } else if (hal::millis() < toastUntil) {
    text(3, BOTTOM + 3, toastText.c_str(), toastBad ? BAD : GOOD, SCREEN_W - 6);
  } else {
    text(3, BOTTOM + 3, p->hints(), DIM, SCREEN_W - 6);
  }
  canvas.pushSprite(&M5Cardputer.Display, 0, 0);  // (the display named here: a global M5Canvas is made before M5Cardputer is)
}

// ------------------------------------------------------------------------------------------- the pages ---
void ui::push(Page* p) {
  stack.push_back(p);
  dirty = true;
}

void ui::pop() {
  if (stack.size() > 1) {
    delete stack.back();
    stack.pop_back();
  }
  dirty = true;
}

void ui::replace(Page* p) {
  delete stack.back();
  stack.back() = p;
  dirty = true;
}

void ui::redraw() {
  dirty = true;
}

void ui::toast(const std::string& s, bool bad) {
  toastText = s;
  toastBad = bad;
  toastUntil = hal::millis() + 3000;
  hal::log("toast: %s", s.c_str());
  dirty = true;
}

// --------------------------------------------------------------------------------------- the firmwares ---
// at power-on, on a thread of its own: the Hub's list (its short copy, or the whole of it), then the SD card's .fwsc
// files, as fm1buddy does (its ui.cpp bootBegin): the UI's thread takes them once they're there
static FwList bootHub, bootSd;
static std::string bootUpdated;
static std::atomic<bool> bootDone{false};
static bool bootRunning, hubNew;
static int bootUnknown;

static void bootBegin() {
  if (!hal::sd()) {
    return;
  }
  bootRunning = true;
  hal::spawn(
      [](void*) {
        std::string err;
        if (!catalog::loadHub(bootHub, bootUpdated)) {
          catalog::parseHub(bootHub, bootUpdated, err);
        }
        bootUnknown = catalog::scanSdInto(bootHub, bootSd);
        hal::log("boot: hub %u firmwares, sd %u", (unsigned)bootHub.size(), (unsigned)bootSd.size());
        bootDone = true;
      },
      0);
}

static void bootWatch() {
  if (!bootRunning || !bootDone) {
    return;
  }
  bootRunning = false;
  if (hubNew) {
    catalog::scanSd();
  } else {
    catalog::hub.swap(bootHub);
    catalog::hubUpdated = bootUpdated;
    catalog::sd.swap(bootSd);
  }
  FwList().swap(bootHub);
  FwList().swap(bootSd);
  if (bootUnknown) {
    catalog::hashSd();
  }
  dirty = true;
}

static void watch() {
  if (!bootRunning && catalog::sdHashed()) {
    catalog::scanSd();
    dirty = true;
  }
  if (seenFinished != jobs::finished) {
    seenFinished = jobs::finished;
    if (jobs::ok && jobs::kind == jobs::HUB) {
      catalog::hub.swap(jobs::hub);
      FwList().swap(jobs::hub);
      hubNew = true;
      catalog::hubUpdated = jobs::hubUpdated;
      catalog::scanSd();
    } else if (jobs::kind == jobs::DOWNLOAD || jobs::kind == jobs::INSTALL || jobs::kind == jobs::FW_UPDATE) {
      catalog::scanSd();
    }
    if (!jobs::ok && !jobs::error.empty()) {
      ui::toast(jobs::error, true);
    }
    stack.back()->jobDone();
    dirty = true;
  }
  fm1::watch();
  uint32_t fm1s = 0;  // the keys' notes leave the FM-1s' own alone (play.h)
  for (int d = 0; d < MIDI_DEVS; d++) {
    fm1s |= fm1::known(d) ? 1u << d : 0;
  }
  play::skip(fm1s);
  bootWatch();
}

// ------------------------------------------------------------------------------------------ the keyboard ---
static std::vector<ui::Key> injected;

void ui::inject(const Key& k) {
  injected.push_back(k);
}

void ui::shot(void (*row)(const uint16_t* px, int n)) {
  std::vector<uint16_t> line(SCREEN_W);
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      line[x] = canvas.readPixel(x, y);
    }
    row(line.data(), SCREEN_W);
  }
}

static bool readKey(ui::Key& k) {
  if (!injected.empty()) {
    k = injected.front();
    injected.erase(injected.begin());
    return true;
  }
  if (!M5Cardputer.Keyboard.isChange()) {
    return false;
  }
  auto s = M5Cardputer.Keyboard.keysState();
  if (stack.back()->raw()) {  // (every change: what's held now)
    std::vector<char> held(s.word.begin(), s.word.end());
    stack.back()->held(s.fn ? std::vector<char>() : held);
  }
  if (!M5Cardputer.Keyboard.isPressed()) {
    return false;
  }
  k = {};
  k.enter = s.enter;
  k.del = s.del;
  k.tab = s.tab;
  bool typing = stack.back()->typing();
  for (char c : s.word) {
    bool nav = s.fn || !typing;
    if (nav && c == ';') {
      k.up = true;
    } else if (nav && c == '.') {
      k.down = true;
    } else if (nav && c == ',') {
      k.left = true;
    } else if (nav && c == '/') {
      k.right = true;
    } else if (nav && c == '`') {
      k.back = true;
    } else if (!k.ch) {
      k.ch = c;
    }
  }
  return k.ch || k.up || k.down || k.left || k.right || k.enter || k.back || k.del || k.tab;
}

void ui::begin() {
  settingsLoad();
  canvas.setColorDepth(4);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  canvas.createPalette();
  canvas.setFont(&fonts::Font0);
  canvas.setTextSize(1);
  canvas.setTextDatum(top_left);
  applyTheme();
  push(homePage());
  // as fm1buddy starts: Bluetooth MIDI and MIDI routing as they were left, the mapping's rules, and the keyboard as a
  // controller that's always there
  bool on = false;
  if (!hal::recoveryMode() && hal::load("bluetooth", &on, 1) && on) {
    hal::bleOn(true);
  }
  play::begin();
  on = false;
  hal::load("routing", &on, 1);
  if (!hal::diskMode() && !hal::recoveryMode()) {
    midi::loadMap();
    midi::mount(MIDI_TOUCH, 0, 0, "Keys");
  }
  midi::route(on && !hal::diskMode() && !hal::recoveryMode());
  if (hal::recoveryMode()) {
    push(recoverPage());
  } else if (hal::diskMode()) {
    push(diskPage());
  }
  drawFrame();
  if (!hal::diskMode()) {  // (the computer has the card then)
    bootBegin();
  }
}

void ui::loop() {
  Key k;
  if (readKey(k)) {
    Page* p = stack.back();
    if (k.back && p->closing()) {
      pop();
    } else if (!k.back || stack.back() == p) {
      p->key(k);
    }
    dirty = true;
  }
  if (hal::millis() - lastTick >= 100) {
    lastTick = hal::millis();
    watch();
    stack.back()->tick();
    static bool wasRunning, hadToast;
    bool toasting = hal::millis() < toastUntil;
    if (jobs::running || wasRunning || toasting != hadToast) {
      dirty = true;  // (its status, as it goes)
    }
    wasRunning = jobs::running;
    hadToast = toasting;
  }
  if (dirty) {
    dirty = false;
    drawFrame();
  }
}
