#pragma once
// Analog SIM800L audio <-> Bluetooth HFP PCM bridge, using only the ESP32's
// built-in converters (plus a few resistors/capacitors, see docs/HARDWARE.md):
//
//   earbud mic --HFP PCM--> [gain, 8->16 kHz] --> DAC GPIO25 --RC--> SIM800L MIC+
//   SIM800L SPK+ --RC--> ADC GPIO34 (24/32 kHz DMA) --> [DC block, FIR, decimate] --HFP PCM--> earbuds
#include <stdint.h>

namespace audio {

enum class TestMode : uint8_t {
  None,          // normal call audio
  Tone,          // 1 kHz tone to the earbuds (checks the Bluetooth speaker path)
  MicLoopback,   // earbud mic straight back to the earbud speaker (checks the mic)
};

struct Stats {
  float downDb;          // level going to the earbuds (dBFS)
  float upDb;            // level coming from the earbud mic (dBFS)
  uint32_t dacUnderruns;
  uint32_t hfUnderruns;
  uint32_t overruns;
  bool running;
  uint32_t rate;
};

bool begin();                    // one-time hardware setup
void start(uint32_t hfSampleRate); // 8000 (CVSD) or 16000 (mSBC)
void stop();
bool running();
void setGains(uint8_t earbudSpeakerStep, uint8_t earbudMicStep);  // 0..15, 8 = 0 dB
void setTestMode(TestMode m);
TestMode testMode();
Stats stats();

// Bluetooth HFP "voice over HCI" data callbacks (run in the Bluetooth task).
void onHfIncoming(const uint8_t* buf, uint32_t len);
uint32_t onHfOutgoing(uint8_t* buf, uint32_t len);

}  // namespace audio
