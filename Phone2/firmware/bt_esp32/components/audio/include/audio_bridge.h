#pragma once
// A7672S analog call audio <-> Bluetooth HFP PCM bridge, using only the ESP32's
// built-in converters plus a small RC network (docs/AUDIO_ARCHITECTURE.md):
//
//   earbud mic --HFP PCM--> [gain, 8->16 kHz] --> DAC GPIO25 --RC--> A7672S MIC_P
//   A7672S EAR_P --RC--> ADC GPIO34 (24/32 kHz DMA) --> [DC block, FIR, decimate] --HFP PCM--> earbuds
#include <stdint.h>

#include "btlink_proto.h"

namespace audio {

struct Stats {
  float downDb;          // level going to the earbuds (dBFS)
  float upDb;            // level coming from the earbud mic (dBFS)
  uint32_t dacUnderruns;
  uint32_t hfUnderruns;
  uint32_t overruns;
  bool running;
  uint32_t rate;
};

bool begin();                      // one-time hardware setup
void start(uint32_t hfSampleRate); // 8000 (CVSD) or 16000 (mSBC)
void stop();
bool running();
void setGains(uint8_t earbudSpeakerStep, uint8_t earbudMicStep);  // 0..15, 8 = 0 dB
void setTestMode(btl::AudioTestMode m);
btl::AudioTestMode testMode();
Stats stats();

// Bluetooth HFP "voice over HCI" data callbacks (run in the Bluetooth task).
void onHfIncoming(const uint8_t* buf, uint32_t len);
uint32_t onHfOutgoing(uint8_t* buf, uint32_t len);

}  // namespace audio
