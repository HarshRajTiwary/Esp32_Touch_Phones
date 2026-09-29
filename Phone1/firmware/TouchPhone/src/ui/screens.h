#pragma once
// Screen base class and the navigation stack.
#include <stdint.h>

#include "../config/app_config.h"
#include "../core/events.h"
#include "gui.h"

enum class ScreenId : uint8_t {
  Boot, Lock, Home, Dialer, Call, Messages, Thread, Compose, Contacts, ContactView, ContactEdit,
  Recents, Settings, Bluetooth, Sound, Display, Network, DateTime, Ussd, About, Calibrate, SimPin,
  Count
};

struct NavParam {
  int i = -1;
  int j = 0;
  char s[48] = "";
  static NavParam num(int v) { NavParam p; p.i = v; return p; }
  static NavParam str(const char* v, int i = -1);
};

class Screen {
 public:
  virtual ~Screen() {}
  virtual void enter(const NavParam& p) { (void)p; }
  virtual void resume() {}                      // a child screen was closed
  virtual void frame(Gui& ui) = 0;
  virtual bool animating() const { return false; }  // true = redraw continuously
  virtual void onEvent(const AppEvent& e) { (void)e; }
};

namespace nav {
void add(ScreenId id, Screen* s);
void push(ScreenId id, const NavParam& p = NavParam());
void replace(ScreenId id, const NavParam& p = NavParam());
void back();
void home();
void reset(ScreenId id, const NavParam& p = NavParam());
void remove(ScreenId id);                     // drop every instance from the stack
ScreenId current();
ScreenId previous();
Screen* screen();
Screen* get(ScreenId id);
bool contains(ScreenId id);
uint32_t generation();                        // changes on every navigation
}  // namespace nav

// Contact picker hand-off (Contacts screen opened in "pick" mode).
namespace pick {
extern bool active;
extern bool done;
extern char number[NUMBER_MAX];
extern char name[CONTACT_NAME_MAX];
}  // namespace pick

namespace screens {
void registerAll();
}
