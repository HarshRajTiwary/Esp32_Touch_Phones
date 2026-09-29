#include "nettools.h"

#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "events.h"
#include "modem.h"
#include "plog.h"
#include "text_util.h"

// The phone keeps LOCAL time in the system clock (that is what the network's
// NITZ / AT+CCLK give us). SNTP delivers UTC, so shift it by the network's time
// zone before setting the clock. Overrides lwIP's weak default.
extern "C" void sntp_sync_time(struct timeval* tv) {
  struct timeval local = *tv;
  local.tv_sec += (time_t)modem::tzQuarters() * 15 * 60;
  settimeofday(&local, nullptr);
  sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}

namespace nettools {
namespace {

bool s_sntpStarted = false;
volatile bool s_testRunning = false;
char s_result[96] = "";

void onTimeSync(struct timeval* tv) {
  LOG("TIME", "clock synchronised over the internet (SNTP)");
  events::post(Ev::TimeSynced, (int32_t)tv->tv_sec);
}

void onIpEvent(void*, esp_event_base_t, int32_t id, void*) {
  if (id != IP_EVENT_PPP_GOT_IP || s_sntpStarted) return;
  esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  cfg.sync_cb = onTimeSync;
  if (esp_netif_sntp_init(&cfg) == ESP_OK) s_sntpStarted = true;
}

void testTask(void*) {
  uint32_t t0 = plogMillis();
  esp_http_client_config_t c = {};
  c.url = "http://connectivitycheck.gstatic.com/generate_204";
  c.timeout_ms = 10000;
  esp_http_client_handle_t h = esp_http_client_init(&c);
  esp_err_t e = h ? esp_http_client_perform(h) : ESP_FAIL;
  int status = h ? esp_http_client_get_status_code(h) : 0;
  if (h) esp_http_client_cleanup(h);
  uint32_t ms = plogMillis() - t0;
  if (e == ESP_OK && status == 204) snprintf(s_result, sizeof s_result, "Internet OK (%lu ms)", (unsigned long)ms);
  else if (e == ESP_OK) snprintf(s_result, sizeof s_result, "Reached a server but got HTTP %d (captive portal?)", status);
  else snprintf(s_result, sizeof s_result, "No internet (%s)", esp_err_to_name(e));
  LOG("NET", "connectivity test: %s", s_result);
  s_testRunning = false;
  events::post(Ev::DataChanged);
  vTaskDelete(nullptr);
}

}  // namespace

void begin() { esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, onIpEvent, nullptr); }

void startConnectivityTest() {
  if (s_testRunning) return;
  s_testRunning = true;
  s_result[0] = 0;
  if (xTaskCreate(testTask, "nettest", 6144, nullptr, 3, nullptr) != pdPASS) s_testRunning = false;
}

bool testRunning() { return s_testRunning; }
const char* lastTestResult() { return s_result; }

}  // namespace nettools
