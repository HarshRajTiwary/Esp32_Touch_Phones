#include "screens.h"

#include "../core/log.h"
#include "../core/text_util.h"

namespace pick {
bool active = false;
bool done = false;
char number[NUMBER_MAX] = "";
char name[CONTACT_NAME_MAX] = "";
}  // namespace pick

NavParam NavParam::str(const char* v, int i) {
  NavParam p;
  p.i = i;
  txt::copy(p.s, v, sizeof p.s);
  return p;
}

namespace nav {

namespace {
constexpr int kDepth = 10;
Screen* s_screens[(int)ScreenId::Count] = {};
ScreenId s_stack[kDepth];
NavParam s_params[kDepth];
int s_depth = 0;
uint32_t s_gen = 0;

void enterTop() {
  ui.closeModal();  // never leave a dialog floating over a new screen (e.g. an incoming call)
  ++s_gen;
  Screen* s = s_screens[(int)s_stack[s_depth - 1]];
  if (s) s->enter(s_params[s_depth - 1]);
}
}  // namespace

void add(ScreenId id, Screen* s) { s_screens[(int)id] = s; }

void push(ScreenId id, const NavParam& p) {
  if (s_depth > 0 && s_stack[s_depth - 1] == id) {
    replace(id, p);
    return;
  }
  if (s_depth == kDepth) {  // drop the oldest entry above Home
    for (int k = 1; k < kDepth - 1; ++k) {
      s_stack[k] = s_stack[k + 1];
      s_params[k] = s_params[k + 1];
    }
    --s_depth;
  }
  s_stack[s_depth] = id;
  s_params[s_depth] = p;
  ++s_depth;
  enterTop();
}

void replace(ScreenId id, const NavParam& p) {
  if (s_depth == 0) {
    push(id, p);
    return;
  }
  s_stack[s_depth - 1] = id;
  s_params[s_depth - 1] = p;
  enterTop();
}

void back() {
  ui.closeModal();
  if (s_depth <= 1) {
    if (s_depth == 0 || s_stack[0] != ScreenId::Home) reset(ScreenId::Home);
    return;
  }
  --s_depth;
  ++s_gen;
  Screen* s = s_screens[(int)s_stack[s_depth - 1]];
  if (s) s->resume();
}

void home() { reset(ScreenId::Home); }

void reset(ScreenId id, const NavParam& p) {
  s_depth = 0;
  push(id, p);
}

void remove(ScreenId id) {
  bool wasTop = s_depth > 0 && s_stack[s_depth - 1] == id;
  if (wasTop) ui.closeModal();
  int o = 0;
  for (int k = 0; k < s_depth; ++k) {
    if (s_stack[k] == id) continue;
    s_stack[o] = s_stack[k];
    s_params[o] = s_params[k];
    ++o;
  }
  s_depth = o;
  if (s_depth == 0) {
    reset(ScreenId::Home);
  } else if (wasTop) {
    ++s_gen;
    Screen* s = s_screens[(int)s_stack[s_depth - 1]];
    if (s) s->resume();
  }
}

ScreenId current() { return s_depth ? s_stack[s_depth - 1] : ScreenId::Home; }
ScreenId previous() { return s_depth > 1 ? s_stack[s_depth - 2] : ScreenId::Home; }
Screen* screen() { return s_screens[(int)current()]; }
Screen* get(ScreenId id) { return s_screens[(int)id]; }

bool contains(ScreenId id) {
  for (int k = 0; k < s_depth; ++k)
    if (s_stack[k] == id) return true;
  return false;
}

uint32_t generation() { return s_gen; }

}  // namespace nav
