#include "audio_bridge.h"

#include <Arduino.h>
#include <driver/dac_oneshot.h>
#include <driver/gptimer.h>
#include <esp_adc/adc_continuous.h>
#include <esp_hf_ag_api.h>
#include <hal/dac_ll.h>
#include <math.h>
#include <string.h>

#include "../config/board_config.h"
#include "../core/log.h"
#include "../core/ringbuf.h"
#include "dsp.h"

namespace {

constexpr uint32_t kDacRate = 16000;          // DAC update rate (uplink to SIM800L MIC)
constexpr uint32_t kTimerHz = 8000000;
constexpr uint32_t kAdcFrameBytes = 480;      // multiple of SOC_ADC_DIGI_DATA_BYTES_PER_CONV
// Uplink (DAC) buffer levels at 16 kHz
constexpr uint32_t kUpPrefill = 480;          // 30 ms before the DAC starts playing
constexpr uint32_t kUpHigh = 2400;            // 150 ms: latency is creeping up
constexpr uint32_t kUpTarget = 800;           // trim back to 50 ms
// Downlink (earbuds) buffer levels in samples at the HFP rate
constexpr uint32_t kDownHighMs = 120, kDownTargetMs = 40;

SampleRing<4096> s_up;     // earbud mic -> DAC, 16 kHz
SampleRing<4096> s_down;   // ADC -> earbuds, HFP rate

dac_oneshot_handle_t s_dac = nullptr;
gptimer_handle_t s_timer = nullptr;
adc_continuous_handle_t s_adc = nullptr;
adc_channel_t s_adcChannel = ADC_CHANNEL_6;
TaskHandle_t s_task = nullptr;

volatile bool s_running = false;
volatile bool s_dacPrimed = false;
volatile audio::TestMode s_test = audio::TestMode::None;
uint32_t s_rate = 8000;
uint32_t s_frameBytes = 120;
bool s_timerRunning = false, s_adcRunning = false;

dsp::Decimator s_dec;
dsp::DcBlocker s_dcBlock;
dsp::Upsampler2x s_upsample;
float s_spkGain = 1.0f, s_micGain = 1.0f;
float s_tonePhase = 0;

volatile uint32_t s_dacUnderruns = 0, s_hfUnderruns = 0, s_overruns = 0;
volatile float s_downDb = -96, s_upDb = -96;
double s_upAcc = 0;
uint32_t s_upN = 0;

// DAC output at 16 kHz. Runs from the general-purpose timer ISR.
bool IRAM_ATTR onDacTimer(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
  uint32_t avail = s_up.available();
  if (!s_dacPrimed) {
    if (avail < kUpPrefill) {
      dac_ll_update_output_value(DAC_CHAN_0, 128);
      return false;
    }
    s_dacPrimed = true;
  }
  if (avail > kUpHigh) s_up.skip(avail - kUpTarget);
  int16_t s;
  if (s_up.pop(&s)) {
    int v = (s >> 8) + 128;
    dac_ll_update_output_value(DAC_CHAN_0, (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v));
  } else {
    s_dacPrimed = false;
    s_dacUnderruns = s_dacUnderruns + 1;
    dac_ll_update_output_value(DAC_CHAN_0, 128);
  }
  return false;
}

void configureAdc(uint32_t adcRate) {
  adc_digi_pattern_config_t pattern = {};
  pattern.atten = ADC_ATTEN_DB_12;
  pattern.channel = (uint8_t)s_adcChannel;
  pattern.unit = ADC_UNIT_1;
  pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  adc_continuous_config_t cfg = {};
  cfg.pattern_num = 1;
  cfg.adc_pattern = &pattern;
  cfg.sample_freq_hz = adcRate;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
  esp_err_t e = adc_continuous_config(s_adc, &cfg);
  if (e != ESP_OK) LOG("AUDIO", "adc config failed: %s", esp_err_to_name(e));
}

void audioTask(void*) {
  static uint8_t raw[kAdcFrameBytes];
  static int16_t out[kAdcFrameBytes / 2];
  double acc = 0;
  uint32_t accN = 0;
  for (;;) {
    if (!s_running) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
      continue;
    }
    uint32_t got = 0;
    if (adc_continuous_read(s_adc, raw, sizeof raw, &got, 40) != ESP_OK || !got) continue;
    int n = 0;
    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= got; i += SOC_ADC_DIGI_RESULT_BYTES) {
      const adc_digi_output_data_t* d = (const adc_digi_output_data_t*)&raw[i];
      if (d->type1.channel != (uint32_t)s_adcChannel) continue;
      float x = ((float)d->type1.data - 2048.0f) * 16.0f;  // 12-bit -> int16 scale
      x = s_dcBlock.process(x);
      float y;
      if (s_dec.push(x, &y)) out[n++] = (int16_t)lrintf(dsp::softClip(y * s_spkGain));
    }
    audio::TestMode mode = s_test;
    if (mode == audio::TestMode::MicLoopback) continue;  // downlink is fed from the mic instead
    if (mode == audio::TestMode::Tone) {
      for (int k = 0; k < n; ++k) {
        out[k] = (int16_t)(9000.0f * sinf(s_tonePhase));
        s_tonePhase += 2.0f * 3.14159265f * 1000.0f / (float)s_rate;
        if (s_tonePhase > 6.2831853f) s_tonePhase -= 6.2831853f;
      }
    }
    for (int k = 0; k < n; ++k) acc += (double)out[k] * out[k];
    accN += n;
    if (accN >= s_rate / 10) {
      double rms = sqrt(acc / accN);
      s_downDb = rms < 1 ? -96.0f : (float)(20.0 * log10(rms / 32768.0));
      acc = 0;
      accN = 0;
    }
    if (s_down.write(out, n) < (uint32_t)n) s_overruns = s_overruns + 1;
    if (s_down.available() * 2 >= s_frameBytes) esp_hf_ag_outgoing_data_ready();
  }
}

}  // namespace

namespace audio {

bool begin() {
  dac_oneshot_config_t dc = {};
  dc.chan_id = DAC_CHAN_0;  // GPIO25
  if (dac_oneshot_new_channel(&dc, &s_dac) != ESP_OK) {
    LOG("AUDIO", "DAC init failed");
    return false;
  }
  dac_oneshot_output_voltage(s_dac, 128);

  gptimer_config_t tc = {};
  tc.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  tc.direction = GPTIMER_COUNT_UP;
  tc.resolution_hz = kTimerHz;
  if (gptimer_new_timer(&tc, &s_timer) != ESP_OK) {
    LOG("AUDIO", "timer init failed");
    return false;
  }
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = onDacTimer;
  gptimer_register_event_callbacks(s_timer, &cbs, nullptr);
  gptimer_alarm_config_t ac = {};
  ac.alarm_count = kTimerHz / kDacRate;
  ac.reload_count = 0;
  ac.flags.auto_reload_on_alarm = true;
  gptimer_set_alarm_action(s_timer, &ac);
  gptimer_enable(s_timer);

  adc_unit_t unit;
  if (adc_continuous_io_to_channel(PIN_AUDIO_ADC_IN, &unit, &s_adcChannel) != ESP_OK || unit != ADC_UNIT_1) {
    LOG("AUDIO", "GPIO%d is not an ADC1 pin", PIN_AUDIO_ADC_IN);
    return false;
  }
  adc_continuous_handle_cfg_t hc = {};
  hc.max_store_buf_size = 4096;
  hc.conv_frame_size = kAdcFrameBytes;
  if (adc_continuous_new_handle(&hc, &s_adc) != ESP_OK) {
    LOG("AUDIO", "ADC DMA init failed");
    return false;
  }
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 12, &s_task, 1);
  LOG("AUDIO", "bridge ready: DAC GPIO%d, ADC GPIO%d", PIN_AUDIO_DAC_OUT, PIN_AUDIO_ADC_IN);
  return true;
}

void start(uint32_t hfSampleRate) {
  if (!s_adc || !s_timer) return;
  if (s_running) stop();
  s_rate = hfSampleRate == 16000 ? 16000 : 8000;
  s_frameBytes = s_rate == 16000 ? 240 : 120;
  uint32_t adcRate = s_rate == 16000 ? 32000 : 24000;  // ESP32 ADC DMA needs >= 20 kHz
  s_dec.design((int)adcRate, (int)(adcRate / s_rate), 3600.0f);
  s_dcBlock.reset();
  s_upsample.reset();
  s_up.clear();
  s_down.clear();
  s_dacPrimed = false;
  s_upAcc = 0;
  s_upN = 0;
  configureAdc(adcRate);
  s_adcRunning = adc_continuous_start(s_adc) == ESP_OK;
  s_timerRunning = gptimer_start(s_timer) == ESP_OK;
  s_running = true;
  xTaskNotifyGive(s_task);
  LOG("AUDIO", "bridge started, %s %u Hz", s_rate == 16000 ? "mSBC" : "CVSD", (unsigned)s_rate);
}

void stop() {
  if (!s_running) return;
  s_running = false;
  if (s_timerRunning) gptimer_stop(s_timer);
  if (s_adcRunning) adc_continuous_stop(s_adc);
  s_timerRunning = s_adcRunning = false;
  dac_ll_update_output_value(DAC_CHAN_0, 128);
  s_downDb = -96;
  s_upDb = -96;
  LOG("AUDIO", "bridge stopped (DAC underruns %u, HF underruns %u, overruns %u)", (unsigned)s_dacUnderruns,
      (unsigned)s_hfUnderruns, (unsigned)s_overruns);
}

bool running() { return s_running; }

void setGains(uint8_t spk, uint8_t mic) {
  s_spkGain = dsp::gainFromStep(spk);
  s_micGain = dsp::gainFromStep(mic);
}

void setTestMode(TestMode m) {
  s_test = m;
  s_down.clear();
}

TestMode testMode() { return s_test; }

Stats stats() {
  Stats s;
  s.downDb = s_downDb;
  s.upDb = s_upDb;
  s.dacUnderruns = s_dacUnderruns;
  s.hfUnderruns = s_hfUnderruns;
  s.overruns = s_overruns;
  s.running = s_running;
  s.rate = s_rate;
  return s;
}

void onHfIncoming(const uint8_t* buf, uint32_t len) {
  if (!s_running) return;
  bool loop = s_test == TestMode::MicLoopback;
  for (uint32_t i = 0; i + 1 < len; i += 2) {
    int16_t s;
    memcpy(&s, buf + i, 2);  // the HCI payload is not guaranteed to be 2-byte aligned
    s = (int16_t)lrintf(dsp::softClip(s * s_micGain));
    s_upAcc += (double)s * s;
    s_upN++;
    if (loop) {
      s_down.push(s);
    } else if (s_rate == 8000) {
      int16_t pair[2];
      s_upsample.process(s, pair);
      if (s_up.write(pair, 2) < 2) s_overruns = s_overruns + 1;
    } else {
      if (!s_up.push(s)) s_overruns = s_overruns + 1;
    }
  }
  if (s_upN >= s_rate / 10) {
    double rms = sqrt(s_upAcc / s_upN);
    s_upDb = rms < 1 ? -96.0f : (float)(20.0 * log10(rms / 32768.0));
    s_upAcc = 0;
    s_upN = 0;
  }
  if (loop && s_down.available() * 2 >= s_frameBytes) esp_hf_ag_outgoing_data_ready();
}

uint32_t onHfOutgoing(uint8_t* buf, uint32_t len) {
  if (!s_running) return 0;
  uint32_t need = len / 2;
  uint32_t avail = s_down.available();
  uint32_t high = s_rate * kDownHighMs / 1000, target = s_rate * kDownTargetMs / 1000;
  if (avail > high) s_down.skip(avail - target);
  if (s_down.available() < need) {
    s_hfUnderruns = s_hfUnderruns + 1;
    return 0;
  }
  int16_t tmp[256];
  uint32_t done = 0;
  while (done < need) {
    uint32_t chunk = need - done > 256 ? 256 : need - done;
    s_down.read(tmp, chunk);
    memcpy(buf + done * 2, tmp, chunk * 2);
    done += chunk;
  }
  return need * 2;
}

}  // namespace audio
