#include "display.h"

#include "../config/board_config.h"
#include "../core/log.h"

namespace {

class PhoneLGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_XPT2046 touch_;

 public:
  PhoneLGFX() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = TFT_SPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = TFT_SPI_WRITE_HZ;
      cfg.freq_read = TFT_SPI_READ_HZ;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_SPI_SCK;
      cfg.pin_mosi = PIN_SPI_MOSI;
      cfg.pin_miso = PIN_SPI_MISO;
      cfg.pin_dc = PIN_TFT_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = cfg.panel_width = TFT_NATIVE_W;
      cfg.memory_height = cfg.panel_height = TFT_NATIVE_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;  // TFT SDO is intentionally not connected
      cfg.invert = TFT_INVERT;
      cfg.rgb_order = TFT_RGB_ORDER;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      panel_.config(cfg);
    }
#if PIN_TFT_BL >= 0
    {
      auto cfg = light_.config();
      cfg.pin_bl = PIN_TFT_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = TFT_BL_PWM_CHANNEL;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
#endif
    {
      auto cfg = touch_.config();
      // Raw 12-bit coordinates; the firmware applies its own calibration.
      cfg.x_min = 0;
      cfg.x_max = 4095;
      cfg.y_min = 0;
      cfg.y_max = 4095;
      cfg.pin_int = PIN_TOUCH_IRQ;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = TFT_SPI_HOST;
      cfg.freq = TOUCH_SPI_HZ;
      cfg.pin_sclk = PIN_SPI_SCK;
      cfg.pin_mosi = PIN_SPI_MOSI;
      cfg.pin_miso = PIN_SPI_MISO;
      cfg.pin_cs = PIN_TOUCH_CS;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};

PhoneLGFX s_lcd;
LGFX_Sprite* s_canvas = nullptr;
bool s_asleep = false;
uint8_t s_brightness = 200;
uint8_t s_rotation = TFT_ROTATION;

}  // namespace

namespace display {

bool begin(uint8_t brightness, uint8_t rotation) {
  s_brightness = brightness;
  if (!s_lcd.init()) {
    LOG("LCD", "panel init failed");
    return false;
  }
  setRotation(rotation);
  s_lcd.setBrightness(brightness);
  s_lcd.fillScreen(TFT_BLACK);
  if (psramFound()) {
    s_canvas = new LGFX_Sprite(&s_lcd);
    s_canvas->setPsram(true);
    s_canvas->setColorDepth(16);
    if (!s_canvas->createSprite(TFT_WIDTH, TFT_HEIGHT)) {
      delete s_canvas;
      s_canvas = nullptr;
    }
  }
  LOG("LCD", "panel %dx%d (native %dx%d) ready, %s", TFT_WIDTH, TFT_HEIGHT, TFT_NATIVE_W, TFT_NATIVE_H,
      s_canvas ? "PSRAM frame buffer" : "direct drawing (no PSRAM)");
  return true;
}

lgfx::LovyanGFX& gfx() {
  if (s_canvas) return *s_canvas;
  return s_lcd;
}

lgfx::LGFX_Device& panel() { return s_lcd; }

void setRotation(uint8_t r) {
  s_rotation = r & 7;
  s_lcd.setRotation(s_rotation);
  s_lcd.fillScreen(TFT_BLACK);  // clears the uninitialised-memory noise
  int w = s_lcd.width(), h = s_lcd.height();
  if (w != TFT_WIDTH || h != TFT_HEIGHT)
    LOG("LCD", "rotation %u gives %dx%d, UI needs %dx%d - use an odd rotation (1/3/5/7)", s_rotation, w, h, TFT_WIDTH,
        TFT_HEIGHT);
  else
    LOG("LCD", "rotation %u (%dx%d)", s_rotation, w, h);
}

uint8_t rotation() { return s_rotation; }

bool buffered() { return s_canvas != nullptr; }

void present() {
  if (s_canvas && !s_asleep) s_canvas->pushSprite(0, 0);
}

void setBrightness(uint8_t level) {
  s_brightness = level < 10 ? 10 : level;
  if (!s_asleep) s_lcd.setBrightness(s_brightness);
}

void sleep(bool on) {
  if (on == s_asleep) return;
  s_asleep = on;
  if (on) {
    s_lcd.setBrightness(0);
    s_lcd.sleep();
  } else {
    s_lcd.wakeup();
    s_lcd.setBrightness(s_brightness);
  }
}

bool asleep() { return s_asleep; }

}  // namespace display
