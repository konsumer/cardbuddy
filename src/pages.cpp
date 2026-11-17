// The pages: home, the firmware lists (the SD card's and the Hub's) and a firmware's details, Wi-Fi, settings, about
#include <M5Cardputer.h>
#include <strings.h>

#include <algorithm>
#include <vector>

#include "catalog.h"
#include "fm1.h"
#include "hal.h"
#include "jobs.h"
#include "midi.h"
#include "settings.h"
#include "themes.h"
#include "ui.h"

using namespace ui;

static bool lower(char a, char b) {
  return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static bool has(const char* s, const std::string& q) {
  return q.empty() || std::search(s, s + strlen(s), q.begin(), q.end(), lower) != s + strlen(s);
}

static std::string size(uint32_t n) {
  char b[16];
  if (n >= 1024 * 1024) {
    snprintf(b, sizeof b, "%.1f MB", n / 1048576.0);
  } else {
    snprintf(b, sizeof b, "%u KB", (unsigned)((n + 1023) / 1024));
  }
  return b;
}

// ------------------------------------------------------------------------------------------------- home ---
struct Item {
  const char* name;
  const char* what;
  Page* (*open)();
};

static const Item HOME[] = {
    {"Firmware: SD card", "Install what's in /fm1", []() { return firmwarePage(false); }},
    {"Firmware: Hub", "mutant.cloud's firmwares", []() { return firmwarePage(true); }},
    {"Backup & recover", "The FM-1's whole flash", []() { return backupPage(); }},
    {"Sounds", ".syx, setups, SLOOP", []() { return soundsPage(); }},
    {"MIDI", "Devices, routing", []() { return midiPage(); }},
    {"Keys", "Play it from here", []() { return keysPage(); }},
    {"USB disk", "The SD card, on a computer", []() -> Page* {
       if (!hal::sd() || jobs::running) {
         toast(hal::sd() ? "Busy with something else." : "No SD card.", true);
       } else {
         hal::diskModeSet(true);  // (it restarts as a USB disk)
       }
       return 0;
     }},
    {"Settings", "Wi-Fi, theme, brightness", []() { return settingsPage(); }},
    {"About", "Version, SD card, battery", []() { return aboutPage(); }},
};
#define HOME_N ((int)(sizeof HOME / sizeof HOME[0]))

struct HomePage : Page {
  int sel = 0, scroll = 0;
  const char* title() override { return "cardbuddy"; }
  const char* hints() override { return HOME[sel].what; }  // (what the one chosen is)
  bool closing() override { return false; }
  void key(const Key& k) override {
    if (k.up) {
      sel = (sel + HOME_N - 1) % HOME_N;
    } else if (k.down) {
      sel = (sel + 1) % HOME_N;
    } else if (k.enter || k.right) {
      if (Page* p = HOME[sel].open()) {
        push(p);
      }
    }
  }
  void draw() override {
    listRows(HOME_N, sel, scroll, TOP + 2, 9, [](int i, int y, bool s, void*) { text(6, y + 2, HOME[i].name, s ? TEXT : DIM); }, 0);
  }
};

Page* ui::homePage() {
  return new HomePage();
}

// ------------------------------------------------------------------------------------------- a firmware ---
struct FirmwarePage : Page {
  Firmware f;
  bool fromHub;
  int scroll = 0, maxScroll = 0;  // (in lines of 10: as far as the description's end, found as it's drawn)
  FirmwarePage(const Firmware& f, bool fromHub) : f(f), fromHub(fromHub) {}
  const char* title() override { return f.name.c_str(); }
  const char* hints() override {
    if (fromHub && !f.onSd) {
      return "enter install  d download";
    }
    return "enter install";
  }
  void key(const Key& k) override {
    if (k.up && scroll > 0) {
      scroll--;
    } else if (k.down && scroll < maxScroll) {
      scroll++;
    } else if (k.ch == 'd' && fromHub && !f.onSd) {
      if (!settings.ssid[0]) {
        toast("Set up Wi-Fi first (Settings > Wi-Fi).", true);
      } else if (!jobs::download(f)) {
        toast("Busy with something else.", true);
      }
    } else if (k.enter) {
      install(f);
    }
  }
  void jobDone() override {
    if (jobs::kind == jobs::DOWNLOAD && jobs::ok) {
      f.onSd = true;
      toast("On the SD card: " + jobs::saved);
    } else if (jobs::kind == jobs::INSTALL && jobs::ok) {
      f.onSd = true;
      toast("Installed.");
    }
  }
  void tick() override {
    if (jobs::running && (jobs::kind == jobs::INSTALL || jobs::kind == jobs::DOWNLOAD)) {
      redraw();
    }
  }
  void draw() override {
    char line[80];
    int y = TOP + 3 - scroll * 10;
    snprintf(line, sizeof line, "%s  %s", f.version.c_str(), f.date.c_str());
    text(6, y, line, TEXT);
    y += 11;
    snprintf(line, sizeof line, "by %s", f.author.c_str());
    text(6, y, line, DIM, SCREEN_W - 12);
    y += 11;
    snprintf(line, sizeof line, "%s  %s%s%s", f.product.c_str(), f.size ? size(f.size).c_str() : "",
             f.beta ? "  beta" : "", f.stock ? "  stock" : "");
    text(6, y, line, DIM, SCREEN_W - 12);
    y += 11;
    text(6, y, f.onSd ? ("SD: /fm1/" + f.file).c_str() : "Not on the SD card", f.onSd ? GOOD : WARN, SCREEN_W - 12);
    y += 13;
    int lines = wrap(6, y, SCREEN_W - 12, f.what.c_str(), TEXT, 20);
    maxScroll = std::max(0, (y + scroll * 10 + lines * 10 - BOTTOM + 9) / 10);
  }
};

// -------------------------------------------------------------------------------------- the firmwares ---
// a firmware on the SD card with this file name (the Hub's: what's on the card already)
static bool onSd(const char* file) {
  for (const FwItem& x : catalog::sd.items) {
    if (!strcasecmp(catalog::sd.str(x.file), file)) {
      return true;
    }
  }
  return false;
}

// The SD card's firmwares are a list in memory (catalog::sd: a few). The Hub's are 700 or so: too many to keep here
// (catalog::keepHub is off), so its list stays on the SD card (HUB_LIST): what's found is where each one's line is, and
// only the rows on screen are read
struct FirmwaresPage : Page {
  bool hub;
  std::string q;               // what's typed: the rows with it in their name, author or file
  std::vector<uint32_t> rows;  // the SD card's: indexes in catalog::sd; the Hub's: where their lines are
  struct Shown {
    std::string name, version, file;
    uint8_t flags;
  };
  std::vector<std::pair<uint32_t, Shown>> shown;  // the Hub's rows read so far (only those near the screen)
  FILE* hubFile = 0;                              // HUB_LIST, open while the page is (opening it is slow)
  uint32_t seen = 0xFFFFFFFF;
  size_t seenSize = 0;
  int sel = 0, scroll = 0;
  FirmwaresPage(bool hub) : hub(hub) { autoUpdate(); }
  // Settings > Update the Hub list: the first time it's looked at after power-on, as fm1buddy does (not while
  // Bluetooth MIDI runs: Wi-Fi would pause it)
  void autoUpdate() {
    static bool asked;
    if (hub && settings.hubAuto && !asked && settings.ssid[0] && hal::sd() && !(midi::routing && hal::bleIsOn())) {
      asked = true;
      jobs::hubList(false);
    }
  }
  ~FirmwaresPage() { closeHub(); }
  void closeHub() {
    if (hubFile) {
      fclose(hubFile);
      hubFile = 0;
    }
  }

  const char* title() override { return hub ? "Firmware: Hub" : "Firmware: SD card"; }
  const char* hints() override {
    return hub ? "type to find  tab SD  u update" : "type to find  tab Hub";
  }
  bool closing() override {
    if (!q.empty()) {
      q.clear();
      filter();
      return false;
    }
    return true;
  }
  char device[24] = "";  // the model on the hub ("FM-1", "SMK-37 Elite"): only its firmwares are shown. "": all
  static bool matches(const FwList& l, const FwItem& it, const std::string& q, const char* device) {
    const char* product = l.str(it.product);
    if (device[0] && product[0] && !sameHardware(device, product)) {
      return false;  // (another device's: an SMK-37's with an FM-1 on the hub, or the other way round)
    }
    return has(l.str(it.name), q) || has(l.str(it.author), q) || has(l.str(it.file), q);
  }
  // what's on the hub now ("" if none)
  static void deviceNow(char out[24]) {
    const Fm1Id* id = fm1::known(fm1::current());
    snprintf(out, 24, "%s", id ? id->model : "");
  }
  void filter() {
    rows.clear();
    shown.clear();
    closeHub();  // (it may be a new list)
    deviceNow(device);
    if (hub) {
      catalog::eachHub([](const FwList& one, void* arg) {
        FirmwaresPage* p = (FirmwaresPage*)arg;
        if (matches(one, one[0], p->q, p->device)) {
          p->rows.push_back(one[0].line);
        }
        return true;
      },
                       this);
    } else {
      for (size_t i = 0; i < catalog::sd.size(); i++) {
        if (matches(catalog::sd, catalog::sd[i], q, device)) {
          rows.push_back(i);
        }
      }
    }
    sel = std::min(sel, std::max(0, (int)rows.size() - 1));
    seen = FwList::changes;
    seenSize = catalog::sd.size();
  }
  // the Hub's row i: read with the screen's worth after it, the first time it's needed
  const Shown& hubRow(int i) {
    uint32_t line = rows[i];
    for (auto& s : shown) {
      if (s.first == line) {
        return s.second;
      }
    }
    if (shown.size() > 60) {
      shown.clear();
    }
    if (!hubFile) {
      hubFile = catalog::openHub();
    }
    FwList one;
    for (int k = i; k < (int)rows.size() && k < i + 12; k++) {
      Shown r{"?", "", "", 0};
      if (catalog::hubAt(hubFile, rows[k], one)) {
        r = {one.str(one[0].name), one.str(one[0].version), one.str(one[0].file), one[0].flags};
      }
      shown.push_back({rows[k], r});
    }
    return shown[shown.size() - std::min<int>(12, rows.size() - i)].second;
  }
  void tick() override {
    char now[24];
    deviceNow(now);
    if (seen != FwList::changes || seenSize != catalog::sd.size() || strcmp(now, device)) {
      filter();
      redraw();
    }
  }
  void open() {
    Firmware f;
    if (hub) {
      FwList one;
      if (!catalog::hubAt(rows[sel], one)) {
        toast("Can't read it from the SD card.", true);
        return;
      }
      f = catalog::details(one, 0);
      f.onSd = onSd(f.file.c_str());
    } else {
      f = catalog::details(catalog::sd, rows[sel]);
    }
    push(new FirmwarePage(f, hub));
  }
  void key(const Key& k) override {
    int n = rows.size();
    if (k.up && n) {
      sel = (sel + n - 1) % n;
    } else if (k.down && n) {
      sel = (sel + 1) % n;
    } else if (k.left) {
      sel = std::max(0, sel - 8);
    } else if (k.right) {
      sel = std::max(0, std::min(n - 1, sel + 8));
    } else if (k.tab) {
      hub = !hub;
      sel = scroll = 0;
      filter();
      autoUpdate();
    } else if (k.del && !q.empty()) {
      q.pop_back();
      filter();
    } else if (k.enter && n) {
      open();
    } else if (k.ch == 'u' && hub && q.empty()) {
      if (!settings.ssid[0]) {
        toast("Set up Wi-Fi first (Settings > Wi-Fi).", true);
      } else if (!jobs::hubList()) {
        toast("Busy with something else.", true);
      }
    } else if (k.ch) {
      q += k.ch;
      sel = 0;
      filter();
    }
  }
  void jobDone() override {
    if (jobs::kind == jobs::HUB && jobs::ok) {
      toast("The Hub's list is up to date.");
      filter();
    }
  }
  void draw() override {
    int y0 = TOP + 2;
    if (!q.empty()) {
      std::string s = "find: " + q + "_";
      text(4, y0, s.c_str(), ACCENT);
      y0 += 11;
    }
    if (rows.empty()) {
      const char* why = !hal::sd()                           ? "No SD card."
                        : !q.empty()                         ? "Nothing has that in it."
                        : hub && catalog::hubUpdated.empty() ? "No Hub list yet: u to get it (Wi-Fi)."
                        : device[0]                          ? "None for the device on the hub."
                        : hub                                ? "No Hub list yet: u to get it (Wi-Fi)."
                                                             : "No .fwsc files in /fm1. tab: the Hub's.";
      text(6, y0 + 4, why, DIM, SCREEN_W - 12);
      return;
    }
    listRows(rows.size(), sel, scroll, y0, (BOTTOM - y0) / ROW, [](int i, int y, bool s, void* arg) {
      FirmwaresPage* p = (FirmwaresPage*)arg;
      const char *name, *version;
      bool there, beta;
      if (p->hub) {
        const Shown& r = p->hubRow(i);
        name = r.name.c_str(), version = r.version.c_str();
        there = onSd(r.file.c_str()), beta = r.flags & FwItem::BETA;
      } else {
        const FwList& l = catalog::sd;
        const FwItem& it = l[p->rows[i]];
        name = l.str(it.name), version = l.str(it.version);
        there = false, beta = it.flags & FwItem::BETA;
      }
      uint8_t mark = there ? GOOD : beta ? WARN : 0;
      if (mark) {
        fill(4, y + 4, 3, 3, mark);
      }
      text(10, y + 2, name, s ? TEXT : DIM, 150);
      textRight(SCREEN_W - 6, y + 2, version, s ? DIM : OFF); }, this);
  }
};

Page* ui::firmwarePage(bool hub) {
  FirmwaresPage* p = new FirmwaresPage(hub);
  p->filter();
  return p;
}

// ------------------------------------------------------------------------------------------- text entry ---
struct TextPage : Page {
  const char* t;
  std::string prompt;  // (a copy: it's often made on the spot)
  bool secret, shown = false;
  std::string s;
  void (*done)(const std::string&, void*);
  void* arg;
  TextPage(const char* t, const char* prompt, bool secret, void (*done)(const std::string&, void*), void* arg)
      : t(t), prompt(prompt), secret(secret), done(done), arg(arg) {}
  const char* title() override { return t; }
  const char* hints() override { return "enter ok  fn+esc back"; }
  bool typing() override { return true; }
  void key(const Key& k) override {
    if (k.enter) {
      auto fn = done;
      void* a = arg;
      std::string v = s;
      pop();  // (this page is gone: fn may push another)
      fn(v, a);
    } else if (k.del && !s.empty()) {
      s.pop_back();
    } else if (k.tab && secret) {
      shown = !shown;
    } else if (k.ch) {
      s += k.ch;
    }
  }
  void draw() override {
    wrap(6, TOP + 6, SCREEN_W - 12, prompt.c_str(), DIM, 3);
    fill(4, TOP + 40, SCREEN_W - 8, 16, PANEL);
    std::string v = secret && !shown ? std::string(s.size(), '*') : s;
    text(8, TOP + 44, (v + "_").c_str(), TEXT, SCREEN_W - 16);
    if (secret) {
      text(6, TOP + 62, shown ? "tab: hide password" : "tab: show password", DIM);
    }
  }
};

Page* ui::textPage(const char* title, const char* prompt, bool secret, void (*done)(const std::string&, void*), void* arg) {
  return new TextPage(title, prompt, secret, done, arg);
}

// -------------------------------------------------------------------------------------------------- Wi-Fi ---
static std::string joining, joiningPass;

struct WifiPage : Page {
  std::vector<hal::Ap> aps;
  int sel = 0, scroll = 0;
  bool scanned = false;
  WifiPage() { jobs::scan(); }
  const char* title() override { return "Wi-Fi"; }
  const char* hints() override { return "enter join  r scan again  esc back"; }
  void key(const Key& k) override {
    int n = aps.size();
    if (k.up && n) {
      sel = (sel + n - 1) % n;
    } else if (k.down && n) {
      sel = (sel + 1) % n;
    } else if (k.ch == 'r' && !jobs::running) {
      jobs::scan();
    } else if (k.enter && n && !jobs::running) {
      joining = aps[sel].ssid;
      if (aps[sel].open) {
        join("");
      } else {
        push(textPage("Password", ("For " + joining).c_str(), true, [](const std::string& p, void*) { join(p); }, 0));
      }
    }
  }
  static void join(const std::string& pass) {
    joiningPass = pass;
    if (!jobs::join(joining.c_str(), joiningPass.c_str())) {
      toast("Busy with something else.", true);
    }
  }
  void jobDone() override {
    if (jobs::kind == jobs::SCAN) {
      aps = jobs::aps;
      scanned = true;
      sel = 0;
    } else if (jobs::kind == jobs::JOIN && jobs::ok) {
      snprintf(settings.ssid, sizeof settings.ssid, "%s", joining.c_str());
      snprintf(settings.pass, sizeof settings.pass, "%s", joiningPass.c_str());
      settingsSave();
      toast("Joined " + joining + ": kept for next time.");
    }
  }
  void draw() override {
    int y0 = TOP + 2;
    if (settings.ssid[0]) {
      std::string s = std::string("Using: ") + settings.ssid;
      text(6, y0 + 1, s.c_str(), GOOD, SCREEN_W - 12);
      y0 += 12;
    }
    if (aps.empty()) {
      text(6, y0 + 4, scanned ? "No networks found: r to look again." : "Looking for networks...", DIM);
      return;
    }
    listRows(aps.size(), sel, scroll, y0, (BOTTOM - y0) / ROW, [](int i, int y, bool s, void* arg) {
      WifiPage* p = (WifiPage*)arg;
      const hal::Ap& a = p->aps[i];
      text(8, y + 2, a.ssid.c_str(), s ? TEXT : DIM, 170);
      char r[16];
      snprintf(r, sizeof r, "%s%d", a.open ? "open " : "", a.rssi);
      textRight(SCREEN_W - 6, y + 2, r, s ? DIM : OFF); }, this);
  }
};

Page* ui::wifiPage() {
  return new WifiPage();
}

// ----------------------------------------------------------------------------------------------- settings ---
struct SettingsPage : Page {
  enum { WIFI,
         THEME,
         BRIGHTNESS,
         HUB_AUTO,
         BLUETOOTH,
         COMPUTER,
         N };
  int sel = 0;
  const char* title() override { return "Settings"; }
  const char* hints() override { return sel == WIFI ? "esc back" : "< > change  esc back"; }
  void key(const Key& k) override {
    if (k.up) {
      sel = (sel + N - 1) % N;
      return;
    }
    if (k.down) {
      sel = (sel + 1) % N;
      return;
    }
    if (!k.left && !k.right && !k.enter) {
      return;
    }
    int d = k.left ? -1 : 1;
    switch (sel) {
      case WIFI:
        if (k.enter) {
          push(wifiPage());
        }
        return;
      case THEME:
        settings.theme = (settings.theme + THEME_COUNT + d) % THEME_COUNT;
        break;
      case BRIGHTNESS:
        settings.brightness = std::max(10, std::min(100, settings.brightness + d * 10));
        break;
      case HUB_AUTO:
        settings.hubAuto = !settings.hubAuto;
        break;
      case BLUETOOTH: {
        hal::bleOn(!hal::bleIsOn());
        bool on = hal::bleIsOn();
        hal::save("bluetooth", &on, 1);
        return;
      }
      case COMPUTER:
        hal::computerMidiSet(!hal::computerMidi());  // (it restarts, if a computer is on the port)
        return;
    }
    settingsSave();
    applyTheme();
  }
  void draw() override {
    char b[N][34];
    snprintf(b[WIFI], 34, "%s", settings.ssid[0] ? settings.ssid : "none");
    snprintf(b[THEME], 34, "%s", THEMES[settings.theme % THEME_COUNT].name);
    snprintf(b[BRIGHTNESS], 34, "%d%%", settings.brightness);
    snprintf(b[HUB_AUTO], 34, "%s", settings.hubAuto ? "on" : "off");
    snprintf(b[BLUETOOTH], 34, "%s", hal::bleIsOn() ? "on" : "off");
    snprintf(b[COMPUTER], 34, "%s", hal::computerMidi() ? "USB MIDI" : "serial");
    const char* names[N] = {"Wi-Fi", "Theme", "Brightness", "Hub list at power-on", "Bluetooth MIDI", "On a computer"};
    for (int i = 0; i < N; i++) {
      int y = TOP + 2 + i * ROW;
      if (i == sel) {
        fill(0, y, SCREEN_W, ROW, OFF);
        fill(0, y, 2, ROW, ACCENT);
      }
      text(8, y + 2, names[i], i == sel ? TEXT : DIM);
      textRight(SCREEN_W - 6, y + 2, b[i], i == sel ? ACCENT : DIM);
    }
    for (int i = 0; i < 4; i++) {  // the theme's tag colors
      fill(8 + i * 14, TOP + 4 + N * ROW, 10, 10, BAD + i);
    }
  }
};

Page* ui::settingsPage() {
  return new SettingsPage();
}

// -------------------------------------------------------------------------------------------------- about ---
struct AboutPage : Page {
  unsigned hubN = 0;  // (counted on the SD card: the Hub's list isn't kept in memory)
  AboutPage() {
    catalog::eachHub([](const FwList&, void* arg) {
      (*(unsigned*)arg)++;
      return true;
    },
                     &hubN);
  }
  const char* title() override { return "About"; }
  const char* hints() override { return "esc back"; }
  void key(const Key&) override {}
  void tick() override {
    static uint32_t t;
    if (hal::millis() - t > 1000) {
      t = hal::millis();
      redraw();
    }
  }
  void draw() override {
    char l[64];
    int y = TOP + 4;
    auto line = [&](const char* s, uint8_t c) {
      text(6, y, s, c, SCREEN_W - 12);
      y += 11;
    };
    line("cardbuddy " FM1BUDDY_VERSION, ACCENT);
    line("fm1buddy's core, on the Cardputer", DIM);
    uint64_t total, used;
    if (hal::sdSpace(total, used)) {
      snprintf(l, sizeof l, "SD: %u of %u MB used", (unsigned)(used >> 20), (unsigned)(total >> 20));
      line(l, TEXT);
    } else {
      line("SD: none", BAD);
    }
    snprintf(l, sizeof l, "Firmwares: %u on SD, %u on the Hub", (unsigned)catalog::sd.size(), hubN);
    line(l, TEXT);
    if (!catalog::hubUpdated.empty()) {
      line(("Hub list: " + catalog::hubUpdated).c_str(), DIM);
    }
    snprintf(l, sizeof l, "Battery: %d%%, %u mV%s", M5Cardputer.Power.getBatteryLevel(), (unsigned)hal::batteryMv(),
             M5Cardputer.Power.isCharging() ? ", charging" : "");
    line(l, TEXT);
    snprintf(l, sizeof l, "RAM: %u KB free (biggest %u KB)", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    line(l, DIM);
  }
};

Page* ui::aboutPage() {
  return new AboutPage();
}
