#pragma once
// Optional passive buzzer (PIN_BUZZER): ringtone, message beep, key clicks.
// Non-blocking: patterns are stepped from app::loop().
#include <stdint.h>

namespace ringer {

enum class Pattern : uint8_t { None, Ring, Message, Click, Error };

void begin();
void play(Pattern p);        // Ring repeats until stop()
void stop();
bool ringing();
void service(uint32_t nowMs);

}  // namespace ringer
