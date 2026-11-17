// The screen (240x135) and the keyboard: a stack of pages, drawn into one canvas in the theme's colors (themes.h)
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

struct Firmware;

namespace ui {
// a key, as the pages see it. The Cardputer's ; . , / are up, down, left, right where nothing is being typed (or with
// Fn anywhere), ` is back (Fn+` while typing)
struct Key {
  char ch;  // a character typed (0 if none)
  bool up, down, left, right, enter, back, del, tab;
};

// colors: the palette's entries
enum Color : uint8_t { BG,
                       PANEL,
                       OFF,
                       BEAT,
                       TEXT,
                       DIM,
                       ACCENT,
                       BAD,
                       WARN,
                       GOOD,
                       INFO };

struct Page {
  virtual ~Page() {}
  virtual const char* title() = 0;
  virtual const char* hints() { return "; . move  enter open  ` back"; }
  virtual bool typing() { return false; }  // keys are text (the arrows need Fn)
  virtual void key(const Key& k) = 0;
  virtual void draw() = 0;                 // the body: y from TOP to BOTTOM
  virtual void tick() {}                   // ten times a second
  virtual void jobDone() {}                // a job (jobs.h) finished, this page on screen
  virtual bool closing() { return true; }  // back: false keeps it open (it handled back itself)
  virtual bool raw() { return false; }     // it wants held: the keys down now, on every change (presses and releases)
  virtual void held(const std::vector<char>& keys) {}
};

#define TOP 14
#define BOTTOM 123
#define ROW 12

void begin();
void loop();
void push(Page* p);  // taken: deleted when it's popped
void pop();
void replace(Page* p);
void redraw();
void toast(const std::string& text, bool bad = false);
void applyTheme();

// drawing into the body
void text(int x, int y, const char* s, uint8_t color, int maxW = 0);
void textRight(int x, int y, const char* s, uint8_t color);
void fill(int x, int y, int w, int h, uint8_t color);
void bar(int x, int y, int w, int h, uint32_t done, uint32_t total);
int wrap(int x, int y, int w, const char* s, uint8_t color, int maxLines);  // lines drawn
// a list's rows: scrolls to keep sel on screen
void listRows(int count, int sel, int& scroll, int y0, int rows, void (*row)(int i, int y, bool sel, void* arg), void* arg);

// pages
Page* homePage();
Page* firmwarePage(bool hub);
Page* wifiPage();
Page* settingsPage();
Page* aboutPage();
Page* textPage(const char* title, const char* prompt, bool secret, void (*done)(const std::string& s, void* arg), void* arg);
Page* backupPage();
Page* recoverPage();  // recovery mode (hal::recoveryMode)
Page* diskPage();     // USB disk mode (hal::diskMode)
Page* soundsPage();
Page* midiPage();
Page* keysPage();
void install(const Firmware& f);  // asks, then installs it on the FM-1
}  // namespace ui
