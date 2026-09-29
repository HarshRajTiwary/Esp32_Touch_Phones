// =============================================================================
//  Phone2 - ESP32-S3 phone firmware
//
//  Tasks:
//    main       : UI + application (app::loop): touch, screens, events
//    modem      : A7672S start-up / recovery state machine, AT commands, URCs
//    modem_rx   : UART receive + CMUX demultiplexer
//    data       : 4G PPP dial / reconnect
//    btlink_rx  : link to the ESP32 Bluetooth helper
//    (IDF)      : Wi-Fi / lwIP / event loop for the hotspot
// =============================================================================
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>

#include "board_init.h"
#include "events.h"
#include "nettools.h"
#include "phone_app.h"
#include "plog.h"

extern "C" void app_main(void) {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  plogInit();
  board_init();
  events::init();
  esp_netif_init();
  esp_event_loop_create_default();
  nettools::begin();
  app::setup();
  for (;;) app::loop();
}
