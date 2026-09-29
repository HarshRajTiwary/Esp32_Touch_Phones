// Analog audio settings inside the A7672S.
//
// The module has one microphone input (MIC_P/N) and one speaker output
// (EAR/REC_SPK_P/N). The earbud path taps that same output/input through the
// ESP32 helper, so "earbuds" uses the handset profile.
//
// AT+COUTGAIN / AT+CMICGAIN are saved in the module's flash (AUTO_SAVE), so
// they are only written when the value really changes.
#include <stdio.h>
#include <stdlib.h>

#include "a7672s_internal.h"
#include "plog.h"
#include "text_util.h"

namespace a7672s {
namespace {

int s_outGain = -1;   // value last read from / written to the module
int s_micGain = -1;
AudioRoute s_profile = AudioRoute::Handset;

int readGain(const char* query, const char* prefix) {
  int v = -1;
  exec(query, 3000, [&](const char* l) {
    if (txt::startsWith(l, prefix)) v = atoi(l + strlen(prefix));
  });
  return v;
}

}  // namespace

void audioConfigure() {
  ok("AT+CSDVC=1");  // handset profile until a call says otherwise
  s_profile = AudioRoute::Handset;
  s_outGain = readGain("AT+COUTGAIN?", "+COUTGAIN:");
  s_micGain = readGain("AT+CMICGAIN?", "+CMICGAIN:");
  audioSetSpeakerGain(g_cfg.speakerGain);
  audioSetMicGain(g_cfg.micGain);
  LOG("AUDIO", "modem audio: out gain %d, mic gain %d (0..7)", s_outGain, s_micGain);
}

void audioSetProfile(AudioRoute r) {
  AudioRoute want = r == AudioRoute::Speaker ? AudioRoute::Speaker : AudioRoute::Handset;
  if (want == s_profile) return;
  if (ok(want == AudioRoute::Speaker ? "AT+CSDVC=3" : "AT+CSDVC=1")) {
    s_profile = want;
    LOG("AUDIO", "modem audio profile: %s", want == AudioRoute::Speaker ? "speakerphone" : "handset");
  }
}

void audioSetSpeakerGain(int g) {
  g_cfg.speakerGain = (uint8_t)g;
  if (g == s_outGain) return;
  char cmd[24];
  snprintf(cmd, sizeof cmd, "AT+COUTGAIN=%d", g);
  if (ok(cmd)) s_outGain = g;
}

void audioSetMicGain(int g) {
  g_cfg.micGain = (uint8_t)g;
  if (g == s_micGain) return;
  char cmd[24];
  snprintf(cmd, sizeof cmd, "AT+CMICGAIN=%d", g);
  if (ok(cmd)) s_micGain = g;
}

}  // namespace a7672s
