// The pages for the FM-1 on the USB-C port: backup and recovery (and recovery mode itself), USB disk mode, sounds,
// SLOOP, MIDI, and the keyboard as a MIDI controller
#include <M5Cardputer.h>
#include <dirent.h>
#include <strings.h>

#include <algorithm>
#include <functional>
#include <vector>

#include "catalog.h"
#include "fm1.h"
#include "hal.h"
#include "jobs.h"
#include "midi.h"
#include "sloop.h"
#include "sounds.h"
#include "uboot.h"
#include "ui.h"

using namespace ui;

// the device to speak to: the FM-1 if there's one, else the first USB MIDI device (-1: none)
static int device() {
  int d = fm1::current();
  midi::Dev v;
  for (int i = 0; d < 0 && i < MIDI_USB; i++) {
    if (midi::dev(i, v)) {
      d = i;
    }
  }
  return d;
}

static std::string devName(int d) {
  midi::Dev v;
  if (d >= 0 && midi::dev(d, v)) {
    return v.name;
  }
  return "";
}

static void busy() {
  toast("Busy with something else.", true);
}

// the flash map (jobs.h), 16 x 16 sectors from x, y, each c pixels
static void drawMap(int x, int y, int c) {
  for (int i = 0; i < MAP_SECTORS; i++) {
    uint8_t color = OFF;
    if (jobs::mapCheck[i] == jobs::CHECK_BAD) {
      color = BAD;
    } else if (jobs::mapMark[i] == jobs::MARK_DIFFERS) {
      color = WARN;
    } else if (jobs::mapMark[i] == jobs::MARK_WRITTEN) {
      color = GOOD;
    } else {
      switch (jobs::mapKind[i]) {
        case jobs::MAP_HEAD:
        case jobs::MAP_KEEP:
          color = INFO;
          break;
        case jobs::MAP_DATA:
          color = ACCENT;
          break;
        case jobs::MAP_FREE:
          color = BEAT;
          break;
      }
    }
    fill(x + (i % 16) * c, y + (i / 16) * c, c - 1, c - 1, color);
  }
  if (jobs::mapAt >= 0) {
    int i = jobs::mapAt;
    fill(x + (i % 16) * c, y + (i / 16) * c, c - 1, c - 1, TEXT);
  }
}

// --------------------------------------------------------------------------------------- a list page ---
// rows of text (and a dim note on the right), and what enter (or another key) does with the one chosen
struct Row {
  std::string name, note;
  std::function<void()> enter;
};

struct ListPage : Page {
  std::string t;
  const char* h;
  std::vector<Row> rows;
  int sel = 0, scroll = 0;
  std::string empty = "Nothing here.";
  std::function<void(ListPage&)> fill;         // makes rows (again: a job finished, a device came)
  std::function<bool(char, ListPage&)> other;  // another key, on row sel: true if it took it
  std::function<void(ListPage&)> done;         // a job finished
  uint32_t seenSeq = 0;
  ListPage(const std::string& t, const char* h) : t(t), h(h) {}
  const char* title() override { return t.c_str(); }
  const char* hints() override { return h; }
  void refill() {
    rows.clear();
    if (fill) {
      fill(*this);
    }
    sel = std::min(sel, std::max(0, (int)rows.size() - 1));
    seenSeq = midi::seq;
    redraw();
  }
  void key(const Key& k) override {
    int n = rows.size();
    if (k.up && n) {
      sel = (sel + n - 1) % n;
    } else if (k.down && n) {
      sel = (sel + 1) % n;
    } else if (k.enter && n && rows[sel].enter) {
      rows[sel].enter();
    } else if (k.ch && other) {
      other(k.ch, *this);
    }
  }
  void tick() override {
    if (midi::seq != seenSeq) {
      refill();
    }
  }
  void jobDone() override {
    if (done) {
      done(*this);
    }
    refill();
  }
  void draw() override {
    if (rows.empty()) {
      wrap(6, TOP + 6, SCREEN_W - 12, empty.c_str(), DIM, 6);
      return;
    }
    listRows(rows.size(), sel, scroll, TOP + 2, 9, [](int i, int y, bool s, void* arg) {
      ListPage* p = (ListPage*)arg;
      const Row& r = p->rows[i];
      int w = r.note.empty() ? SCREEN_W - 16 : 150;
      text(8, y + 2, r.name.c_str(), s ? TEXT : DIM, w);
      if (!r.note.empty()) {
        textRight(SCREEN_W - 6, y + 2, r.note.c_str(), s ? DIM : OFF);
      } }, this);
  }
};

// a question: y goes ahead
struct AskPage : Page {
  std::string t, q;
  std::function<void()> yes;
  AskPage(const std::string& t, const std::string& q, std::function<void()> yes) : t(t), q(q), yes(yes) {}
  const char* title() override { return t.c_str(); }
  const char* hints() override { return "y yes  ` no"; }
  void key(const Key& k) override {
    if (k.ch == 'y' || k.ch == 'Y') {
      auto f = yes;
      pop();
      f();
    }
  }
  void draw() override { wrap(6, TOP + 6, SCREEN_W - 12, q.c_str(), TEXT, 9); }
};

static void ask(const std::string& t, const std::string& q, std::function<void()> yes) {
  push(new AskPage(t, q, yes));
}

static bool hasBackup() {
  std::string dir = catalog::path(BACKUPS_DIR);
  DIR* d = dir.empty() ? 0 : opendir(dir.c_str());
  bool any = false;
  while (dirent* e = d ? readdir(d) : 0) {
    size_t n = strlen(e->d_name);
    any |= n > 4 && !strcasecmp(e->d_name + n - 4, ".bin");
  }
  if (d) {
    closedir(d);
  }
  return any;
}

// install f on the FM-1 (the firmware page's i)
void ui::install(const Firmware& f) {
  const Fm1Id* id = fm1::known(fm1::current());
  if (!id) {
    toast("No FM-1 on the USB-C port.", true);
    return;
  }
  std::string q = "Install " + f.name + " " + f.version + " on the " + fm1::machineName() + " (" + id->text + ")?";
  if (!hasBackup()) {
    q += " There's no backup of it on the SD card yet (Backup & recover).";
  }
  Firmware copy = f;
  ask("Install", q, [copy]() {
    if (!jobs::install(copy)) {
      busy();
    }
  });
}

// --------------------------------------------------------------------------------- backup & recover ---
struct Choice {
  std::string path, name, what;
};

// what can go back on it: the SD card's firmwares for the FM-1 or the SMK-37, then the backups, newest first
static std::vector<Choice> choices() {
  std::vector<Choice> out;
  for (const FwItem& f : catalog::sd.items) {
    const char* product = catalog::sd.str(f.product);
    if (!strncmp(product, "FM-1_", 5) || isSmk(product)) {
      const char* v = catalog::sd.str(f.version);
      out.push_back({std::string(FW_DIR "/") + catalog::sd.str(f.file), catalog::sd.str(f.name),
                     f.flags & FwItem::STOCK ? "stock" : *v ? v
                                                            : "firmware"});
    }
  }
  std::string dir = catalog::path(BACKUPS_DIR);
  std::vector<Choice> found;
  if (DIR* d = dir.empty() ? 0 : opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      size_t n = strlen(e->d_name);
      if (n > 4 && !strcasecmp(e->d_name + n - 4, ".bin")) {
        found.push_back({BACKUPS_DIR "/" + std::string(e->d_name), e->d_name, "backup"});
      }
    }
    closedir(d);
  }
  std::sort(found.begin(), found.end(), [](const Choice& a, const Choice& b) {
    long x = atol(a.name.c_str() + 4), y = atol(b.name.c_str() + 4);
    return x != y ? x > y : a.name > b.name;
  });
  out.insert(out.end(), found.begin(), found.end());
  return out;
}

struct BackupPage : ListPage {
  BackupPage() : ListPage("Backup & recover", "enter go  ; . move  ` back") {
    fill = [](ListPage& p) {
      int d = fm1::current();
      const Fm1Id* id = fm1::known(d);
      p.rows.push_back({"Back up the FM-1 now", id ? id->text : "none here", [d]() {
                          if (d < 0) {
                            toast("Plug the FM-1 into the USB-C port (on a powered hub).", true);
                          } else if (!jobs::backup(d)) {
                            busy();
                          }
                        }});
      for (const Choice& c : choices()) {
        std::string path = c.path, name = c.name;
        p.rows.push_back({"Recover: " + c.name, c.what, [path, name]() {
                            ask("Recover", "Put " + name +
                                               " on an FM-1 that won't start? cardbuddy restarts into recovery mode, "
                                               "then says what to do.",
                                [path]() {
                                  if (!jobs::prepare(path)) {
                                    busy();
                                  }
                                });
                          }});
      }
    };
    done = [](ListPage&) {
      if (jobs::kind == jobs::BACKUP && jobs::ok) {
        toast("Saved as " BACKUPS_DIR "/" + jobs::saved);
      }
    };
    refill();
  }
  void tick() override {
    ListPage::tick();
    if (jobs::running && jobs::kind == jobs::BACKUP) {
      redraw();
    }
  }
  void draw() override {
    if (jobs::kind == jobs::BACKUP && (jobs::running || jobs::mapKind[0] != jobs::MAP_UNREAD)) {
      drawMap(SCREEN_W - 16 * 6 - 4, TOP + 6, 6);
      wrap(6, TOP + 6, SCREEN_W - 16 * 6 - 16, jobs::running ? jobs::status().c_str() : "Backed up.", TEXT, 8);
      return;
    }
    ListPage::draw();
  }
};

Page* ui::backupPage() {
  return new BackupPage();
}

// recovery mode: the board started for it (jobs::prepare), the FM-1 straight on the USB-C port
struct RecoverPage : Page {
  bool started = false;
  const char* title() override { return "Recovery"; }
  const char* hints() override {
    if (jobs::running) {
      return "leave it plugged in";
    }
    return jobs::kind == jobs::RECOVER && !jobs::ok && jobs::calLost ? "a again  s it's an SMK-37  enter done"
                                                                     : "a again  enter done";
  }
  bool closing() override { return false; }
  void key(const Key& k) override {
    if (jobs::running) {
      return;
    }
    if (k.enter) {
      hal::recoveryModeSet(false);  // (it restarts as it was)
    } else if (k.ch == 'a') {
      hal::recoveryModeSet(true);  // (again, from the start)
    } else if (k.ch == 's' && jobs::calLost) {
      jobs::giveCalibration();
      hal::recoveryModeSet(true);
    }
  }
  void tick() override {
    if (!started) {
      started = jobs::recover();
    }
    redraw();
  }
  void draw() override {
    std::string from = jobs::recovering();
    std::string name = from.substr(from.rfind('/') + 1);
    int st = jobs::step;
    bool onFlash = st == jobs::READING_FLASH || st == jobs::SAVING || st == jobs::WRITING_FLASH ||
                   st == jobs::CHECKING_FLASH || st == jobs::LEAVING;
    bool mine = jobs::kind == jobs::RECOVER;
    int w = SCREEN_W - 12;
    if (mine && (jobs::running ? onFlash : jobs::mapKind[0] != jobs::MAP_UNREAD)) {
      drawMap(SCREEN_W - 16 * 6 - 4, TOP + 6, 6);
      w = SCREEN_W - 16 * 6 - 16;
    }
    int y = TOP + 4;
    text(6, y, name.c_str(), ACCENT, w);
    y += 12;
    std::string s;
    uint8_t c = TEXT;
    if (jobs::running) {
      s = jobs::status();
      bool waiting = st == jobs::GETTING_READY || st == jobs::KEYING;
      if (waiting && hal::keyState() < hal::KEY_ANSWERED) {
        s += "\nSwitch the FM-1 off, plug it straight into the USB-C port (no hub), then switch it on.";
      }
    } else if (mine && jobs::ok) {
      s = jobs::error.empty() ? "Done: it's on the FM-1." : jobs::error;
      s += "\nSwitch it off and on: it starts what was written. Then enter.";
      c = GOOD;
    } else if (mine) {
      s = jobs::error;
      c = BAD;
    } else {
      s = "Starting...";
    }
    y += 10 * wrap(6, y, w, s.c_str(), c, 7);
    if (mine && !jobs::saved.empty()) {
      wrap(6, y + 2, w, ("Its flash as it was: " BACKUPS_DIR "/" + jobs::saved).c_str(), DIM, 2);
    }
  }
};

Page* ui::recoverPage() {
  return new RecoverPage();
}

// USB disk mode: the SD card is a computer's drive
struct DiskPage : Page {
  const char* title() override { return "USB disk"; }
  const char* hints() override { return "enter done"; }
  bool closing() override { return false; }
  void key(const Key& k) override {
    if (k.enter) {
      hal::diskModeSet(false);
    }
  }
  void tick() override { redraw(); }
  void draw() override {
    int s = hal::diskState();
    const char* what = s == hal::DISK_MOUNTED   ? "A computer has the SD card. Eject it there before you're done."
                       : s == hal::DISK_EJECTED ? "Ejected: it's safe. Enter to go back."
                                                : "Plug the Cardputer into a computer: the SD card shows up as a drive.";
    wrap(6, TOP + 6, SCREEN_W - 12, what, s == hal::DISK_EJECTED ? GOOD : TEXT, 6);
  }
};

Page* ui::diskPage() {
  return new DiskPage();
}

// ---------------------------------------------------------------------------------------------- sounds ---
static sounds::Speaks speaks;
static int speaksDev = -1;

static Page* syxPage(const std::string& folder);
static Page* setupsPage();
static Page* sloopPage();

static Page* syxPage(const std::string& folder) {
  ListPage* p = new ListPage(folder.empty() ? "Sounds: /syx" : "/syx/" + folder, "enter send  ` back");
  p->empty = "No .syx files here (" SYX_DIR " on the SD card).";
  p->fill = [folder](ListPage& p) {
    for (const sounds::File& f : sounds::list(folder)) {
      std::string path = folder.empty() ? f.name : folder + "/" + f.name;
      if (f.folder) {
        p.rows.push_back({f.name + "/", "", [path]() { push(syxPage(path)); }});
        continue;
      }
      char kb[16];
      snprintf(kb, sizeof kb, "%u KB", (unsigned)((f.size + 1023) / 1024));
      p.rows.push_back({f.name, kb, [path]() {
                          int d = device();
                          if (d < 0) {
                            toast("Nothing on the USB-C port to send it to.", true);
                          } else if (!jobs::send(d, path)) {
                            busy();
                          }
                        }});
    }
  };
  p->done = [](ListPage&) {
    if (jobs::kind == jobs::SEND && jobs::ok) {
      toast("Sent.");
    }
  };
  p->refill();
  return p;
}

static Page* setupsPage() {
  ListPage* p = new ListPage("Setups", "enter load  e export  ` back");
  p->empty = "No setups yet: Sounds > Keep its song and kits.";
  p->fill = [](ListPage& p) {
    for (const std::string& s : sounds::setups()) {
      p.rows.push_back({s, "", [s]() {
                          int d = device();
                          ask("Load", "Load setup " + s + " onto " + devName(d) + "? It saves it.", [d, s]() {
                            if (d < 0 || !jobs::restore(d, s)) {
                              busy();
                            }
                          });
                        }});
    }
  };
  p->other = [](char c, ListPage& p) {
    if (c == 'e' && !p.rows.empty() && !jobs::exportSetup(p.rows[p.sel].name, speaks.app[0] ? speaks.app : "FOMAJAM")) {
      busy();
    }
    return c == 'e';
  };
  p->done = [](ListPage&) {
    if (jobs::ok && (jobs::kind == jobs::RESTORE || jobs::kind == jobs::EXPORT)) {
      toast(jobs::kind == jobs::RESTORE ? "Loaded: it's saved on it." : jobs::saved);
    }
  };
  p->refill();
  return p;
}

struct SoundsPage : ListPage {
  SoundsPage() : ListPage("Sounds", "enter go  p ask again  ` back") {
    fill = [](ListPage& p) {
      int d = device();
      if (d < 0) {
        p.empty = "Plug the FM-1 (or any MIDI device) into the USB-C port, on a powered hub.";
        return;
      }
      if (d != speaksDev && !jobs::running) {
        speaksDev = d;
        memset(&speaks, 0, sizeof speaks);
        jobs::probe(d);
      }
      p.rows.push_back({"Send a .syx", devName(d), []() { push(syxPage("")); }});
      if (speaks.stockVoice) {
        p.rows.push_back({"Save its voice", ".syx", []() {
                            if (!jobs::voice(device())) {
                              busy();
                            }
                          }});
      }
      if (speaks.share) {
        p.rows.push_back({"Keep its song and kits", speaks.app, []() {
                            if (!jobs::keep(device())) {
                              busy();
                            }
                          }});
      }
      p.rows.push_back({"Setups", "load, export", []() { push(setupsPage()); }});
      if (speaks.sloop) {
        p.rows.push_back({"SLOOP", speaks.version, []() { push(sloopPage()); }});
      }
    };
    other = [](char c, ListPage& p) {
      if (c == 'p' && device() >= 0 && !jobs::probe(device())) {
        busy();
      }
      return c == 'p';
    };
    done = [](ListPage&) {
      if (jobs::kind == jobs::PROBE) {
        speaks = jobs::speaks;
      } else if (jobs::ok && jobs::kind == jobs::VOICE) {
        toast("Saved as " SYX_DIR "/" + jobs::saved);
      } else if (jobs::ok && jobs::kind == jobs::KEEP) {
        toast("Saved as " SETUPS_DIR "/" + jobs::saved);
      }
    };
    refill();
  }
};

Page* ui::soundsPage() {
  return new SoundsPage();
}

// ----------------------------------------------------------------------------------------------- SLOOP ---
static Page* sloopFiles(bool songs) {
  ListPage* p = new ListPage(songs ? "SLOOP: songs" : "SLOOP: backups",
                             songs ? "enter load  ` back" : "enter restore  0 a-d project  ` back");
  p->empty = songs ? "No songs in " SLOOP_SONGS_DIR "." : "No backups in " SLOOP_DIR ".";
  p->fill = [songs](ListPage& p) {
    for (const std::string& f : songs ? sloop::songs() : sloop::backups()) {
      p.rows.push_back({f, "", [f, songs]() {
                          ask(songs ? "Load song" : "Restore",
                              songs ? "Load " + f + " onto SLOOP? Its song is saved first."
                                    : "Put all of " + f + " back on SLOOP? It's backed up first.",
                              [f, songs]() {
                                int d = device();
                                if (d < 0 || !(songs ? jobs::sloopSongIn(d, f) : jobs::sloopRestore(d, f))) {
                                  busy();
                                }
                              });
                        }});
    }
  };
  if (!songs) {  // one project: 0 the working one, a-d sections A-D
    p->other = [](char c, ListPage& p) {
      int id = c == '0' ? 0 : c >= 'a' && c <= 'd' ? 2 + c - 'a'
                                                   : -1;
      if (id >= 0 && !p.rows.empty() && !jobs::sloopProject(device(), p.rows[p.sel].name, id, id)) {
        busy();
      }
      return id >= 0;
    };
  }
  p->done = [](ListPage&) {
    if (jobs::ok) {
      toast("Done.");
    }
  };
  p->refill();
  return p;
}

static Page* sloopSamples() {
  ListPage* p = new ListPage("SLOOP: samples", "1-4 into that slot  ` back");
  p->empty = "No .wav files or folders of them in " SLOOP_DIR "/samples.";
  p->fill = [](ListPage& p) {
    for (const std::string& s : sloop::sampleSources()) {
      p.rows.push_back({s, "", 0});
    }
  };
  p->other = [](char c, ListPage& p) {
    if (c >= '1' && c <= '4' && !p.rows.empty() && !jobs::sloopSamples(device(), p.rows[p.sel].name, c - '1')) {
      busy();
    }
    return c >= '1' && c <= '4';
  };
  p->done = [](ListPage&) {
    if (jobs::ok) {
      toast("In the slot.");
    }
  };
  p->refill();
  return p;
}

static Page* sloopPage() {
  ListPage* p = new ListPage("SLOOP", "enter go  ` back");
  p->fill = [](ListPage& p) {
    p.rows.push_back({"Back it up", SLOOP_DIR, []() {
                        if (!jobs::sloopBackup(device())) {
                          busy();
                        }
                      }});
    p.rows.push_back({"Save its song", SLOOP_SONGS_DIR, []() {
                        if (!jobs::sloopSong(device())) {
                          busy();
                        }
                      }});
    p.rows.push_back({"Backups", "restore, projects", []() { push(sloopFiles(false)); }});
    p.rows.push_back({"Songs", "load", []() { push(sloopFiles(true)); }});
    p.rows.push_back({"Samples", "into a slot", []() { push(sloopSamples()); }});
  };
  p->done = [](ListPage&) {
    if (jobs::ok && (jobs::kind == jobs::SLOOP_BACKUP || jobs::kind == jobs::SLOOP_SONG)) {
      toast("Saved as " + jobs::saved);
    }
  };
  p->refill();
  return p;
}

// ------------------------------------------------------------------------------------------------ MIDI ---
static void saveFlag(const char* key, bool on) {
  hal::save(key, &on, 1);
}

struct MidiPage : ListPage {
  MidiPage() : ListPage("MIDI", "r routing  b bluetooth  k keys  ` back") {
    fill = [](ListPage& p) {
      midi::Dev v;
      for (int d = 0; d < MIDI_DEVS; d++) {
        if (!midi::dev(d, v)) {
          continue;
        }
        char last[32] = "";
        if (v.count) {
          midi::describe(v.last, last, sizeof last);
        }
        p.rows.push_back({v.name, last, 0});
      }
      p.empty = "Nothing on the USB-C port: a powered hub, with the FM-1 and controllers on it.";
    };
    other = [](char c, ListPage& p) {
      if (c == 'r') {
        midi::route(!midi::routing);
        saveFlag("routing", midi::routing);
      } else if (c == 'b') {
        hal::bleOn(!hal::bleIsOn());
        saveFlag("bluetooth", hal::bleIsOn());
      } else if (c == 'k') {
        push(keysPage());
      }
      return c == 'r' || c == 'b' || c == 'k';
    };
    refill();
  }
  void tick() override {
    ListPage::tick();
    static uint32_t t;
    if (hal::millis() - t > 300) {  // (what each sent last)
      t = hal::millis();
      refill();
    }
  }
  void draw() override {
    char s[48];
    snprintf(s, sizeof s, "routing %s   bluetooth %s", midi::routing ? "on" : "off", hal::bleIsOn() ? "on" : "off");
    text(6, TOP + 2, s, midi::routing ? GOOD : WARN);
    // the devices under it
    if (rows.empty()) {
      wrap(6, TOP + 16, SCREEN_W - 12, empty.c_str(), DIM, 6);
      return;
    }
    listRows(rows.size(), sel, scroll, TOP + 14, 8, [](int i, int y, bool s, void* arg) {
      ListPage* p = (ListPage*)arg;
      text(8, y + 2, p->rows[i].name.c_str(), s ? TEXT : DIM, 110);
      textRight(SCREEN_W - 6, y + 2, p->rows[i].note.c_str(), s ? DIM : OFF); }, this);
  }
};

Page* ui::midiPage() {
  return new MidiPage();
}

// ------------------------------------------------------------------------------------- the keyboard ---
// The keyboard as a MIDI controller (device MIDI_TOUCH, "Keys"): two rows, as trackers have it. z s x d c v g b h n j m
// , l . ; / play from C, q 2 w 3 e r 5 t 6 y 7 u i 9 o 0 p an octave up. - = change the octave, [ ] the channel.
// Notes go wherever MIDI routing sends them (it's turned on here)
static const char LOWER[] = "zsxdcvgbhnjm,l.;/";
static const char UPPER[] = "q2w3er5t6y7ui9o0p";

struct KeysPage : Page {
  int octave = 4, channel = 0;
  bool down[128] = {};
  const char* title() override { return "Keys"; }
  const char* hints() override { return "- = octave  [ ] channel  fn+` back"; }
  bool typing() override { return true; }  // (every key is a note: fn for the arrows, and back)
  bool raw() override { return true; }
  KeysPage() {
    midi::route(true);
  }
  ~KeysPage() {
    allOff();
  }
  int noteOf(char c) {
    const char* l = strchr(LOWER, c);
    const char* u = strchr(UPPER, c);
    int n = l ? l - LOWER : u ? 12 + (u - UPPER)
                              : -1;
    return n < 0 || !c ? -1 : std::min(127, (octave + 1) * 12 + n);
  }
  void send(int note, bool on) {
    uint8_t m[3] = {(uint8_t)((on ? 0x90 : 0x80) | channel), (uint8_t)note, (uint8_t)(on ? 100 : 0)};
    midi::bytesIn(MIDI_TOUCH, m, 3);
    down[note] = on;
  }
  void allOff() {
    for (int n = 0; n < 128; n++) {
      if (down[n]) {
        send(n, false);
      }
    }
  }
  // the keys held now: notes start and stop as they change
  void held(const std::vector<char>& keys) override {
    bool now[128] = {};
    for (char c : keys) {
      int n = noteOf(c);
      if (n >= 0) {
        now[n] = true;
      }
    }
    for (int n = 0; n < 128; n++) {
      if (now[n] != down[n]) {
        send(n, now[n]);
      }
    }
    redraw();
  }
  void key(const Key& k) override {
    if (k.ch == '-' || k.ch == '=') {
      allOff();
      octave = std::max(0, std::min(8, octave + (k.ch == '=' ? 1 : -1)));
    } else if (k.ch == '[' || k.ch == ']') {
      allOff();
      channel = (channel + (k.ch == ']' ? 1 : 15)) % 16;
    }
  }
  void draw() override {
    char s[48];
    snprintf(s, sizeof s, "octave %d   channel %d   %s", octave, channel + 1, midi::routing ? "" : "(routing off)");
    text(6, TOP + 2, s, ACCENT);
    // two octaves of keys, lit while held
    static const bool black[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    int base = (octave + 1) * 12, x = 6, w = 16, y = TOP + 20;
    for (int i = 0, wi = 0; i < 24; i++) {
      if (!black[i % 12]) {
        fill(x + wi * w, y, w - 1, 70, base + i < 128 && down[base + i] ? ACCENT : TEXT);
        wi++;
      }
    }
    for (int i = 0, wi = 0; i < 24; i++) {
      if (black[i % 12]) {
        fill(x + wi * w - 5, y, 10, 42, base + i < 128 && down[base + i] ? ACCENT : BG);
      } else {
        wi++;
      }
    }
  }
};

Page* ui::keysPage() {
  return new KeysPage();
}
