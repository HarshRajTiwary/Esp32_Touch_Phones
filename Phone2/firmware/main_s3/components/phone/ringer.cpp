#include "ringer.h"

#include <driver/ledc.h>

#include "board_config.h"
#include "plog.h"

namespace ringer {
namespace {

struct Note {
  uint16_t hz;   // 0 = silence
  uint16_t ms;
};

// Two-tone ring (repeats), short message chirp, key click, error buzz
const Note kRing[] = {{1400, 120}, {1100, 120}, {1400, 120}, {1100, 120}, {0, 1500}};
const Note kMessage[] = {{1800, 70}, {0, 40}, {2400, 90}};
const Note kClick[] = {{3000, 8}};
const Note kError[] = {{400, 180}};

const Note* s_seq = nullptr;
int s_len = 0, s_idx = 0;
bool s_loop = false;
uint32_t s_nextMs = 0;
bool s_ok = false;
Pattern s_current = Pattern::None;

constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kChan = LEDC_CHANNEL_1;

void tone(uint16_t hz) {
  if (!s_ok) return;
  if (!hz) {
    ledc_set_duty(kMode, kChan, 0);
    ledc_update_duty(kMode, kChan);
    return;
  }
  ledc_set_freq(kMode, kTimer, hz);
  ledc_set_duty(kMode, kChan, 512);  // 50 % of 10-bit
  ledc_update_duty(kMode, kChan);
}

}  // namespace

void begin() {
#if PIN_BUZZER >= 0
  ledc_timer_config_t t = {};
  t.speed_mode = kMode;
  t.duty_resolution = LEDC_TIMER_10_BIT;
  t.timer_num = kTimer;
  t.freq_hz = 2000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ledc_channel_config_t c = {};
  c.gpio_num = PIN_BUZZER;
  c.speed_mode = kMode;
  c.channel = kChan;
  c.timer_sel = kTimer;
  c.duty = 0;
  s_ok = ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&c) == ESP_OK;
  if (!s_ok) LOG("RING", "buzzer init failed");
#endif
}

void play(Pattern p) {
  if (!s_ok) return;
  if (s_current == Pattern::Ring && p != Pattern::Ring) return;  // never interrupt the ringtone
  switch (p) {
    case Pattern::Ring: s_seq = kRing; s_len = sizeof kRing / sizeof kRing[0]; break;
    case Pattern::Message: s_seq = kMessage; s_len = sizeof kMessage / sizeof kMessage[0]; break;
    case Pattern::Click: s_seq = kClick; s_len = 1; break;
    case Pattern::Error: s_seq = kError; s_len = 1; break;
    default: stop(); return;
  }
  s_loop = p == Pattern::Ring;
  s_current = p;
  s_idx = 0;
  s_nextMs = plogMillis() + s_seq[0].ms;
  tone(s_seq[0].hz);
}

void stop() {
  s_seq = nullptr;
  s_current = Pattern::None;
  tone(0);
}

bool ringing() { return s_current == Pattern::Ring; }

void service(uint32_t now) {
  if (!s_seq || (int32_t)(now - s_nextMs) < 0) return;
  if (++s_idx >= s_len) {
    if (!s_loop) {
      stop();
      return;
    }
    s_idx = 0;
  }
  tone(s_seq[s_idx].hz);
  s_nextMs = now + s_seq[s_idx].ms;
}

}  // namespace ringer
