#include "hotspot.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <lwip/ip4_addr.h>
#include <stdio.h>
#include <string.h>

#include "events.h"
#include "modem.h"
#include "plog.h"
#include "s3_config.h"
#include "settings.h"
#include "text_util.h"

namespace hotspot {
namespace {

esp_netif_t* s_ap = nullptr;
bool s_inited = false;
volatile bool s_on = false;
volatile int s_clients = 0;
volatile bool s_sharing = false;
uint32_t s_upSince = 0;
char s_lastError[48] = "";
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// Public resolver used only until the mobile network tells us its own DNS server.
constexpr uint32_t kFallbackDns = ESP_IP4TOADDR(8, 8, 8, 8);

esp_netif_t* ppp() { return static_cast<esp_netif_t*>(modem::pppNetif()); }

// Hands the mobile network's DNS server to hotspot clients via DHCP.
void updateDhcpDns() {
  if (!s_ap) return;
  esp_netif_dns_info_t dns = {};
  bool havePpp = ppp() && esp_netif_get_dns_info(ppp(), ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
                 dns.ip.u_addr.ip4.addr != 0;
  if (!havePpp) {
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = kFallbackDns;
  }
  esp_netif_dhcps_stop(s_ap);
  esp_netif_set_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &dns);
  uint8_t offer = 0x02;  // OFFER_DNS
  esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof offer);
  esp_netif_dhcps_start(s_ap);
}

void setSharing(bool on) {
  if (s_sharing == on) return;
  s_sharing = on;
  LOG("HOTSPOT", on ? "sharing the 4G connection" : "4G connection down - hotspot has no internet (will resume)");
  events::post(Ev::HotspotChanged, s_clients);
}

void onWifiEvent(void*, esp_event_base_t, int32_t id, void* data) {
  switch (id) {
    case WIFI_EVENT_AP_START:
      if (esp_netif_napt_enable(s_ap) != ESP_OK) LOG("HOTSPOT", "NAPT enable failed");
      updateDhcpDns();
      setSharing(modem::data().state == DataState::Connected);
      break;
    case WIFI_EVENT_AP_STACONNECTED: {
      auto* e = static_cast<wifi_event_ap_staconnected_t*>(data);
      s_clients = s_clients + 1;
      LOG("HOTSPOT", "device connected (%02X:%02X:..:%02X), %d total", e->mac[0], e->mac[1], e->mac[5], s_clients);
      events::post(Ev::HotspotChanged, s_clients);
      break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED:
      if (s_clients > 0) s_clients = s_clients - 1;
      LOG("HOTSPOT", "device disconnected, %d left", s_clients);
      events::post(Ev::HotspotChanged, s_clients);
      break;
    default:
      break;
  }
}

void onIpEvent(void*, esp_event_base_t, int32_t id, void*) {
  if (!s_on) return;
  if (id == IP_EVENT_PPP_GOT_IP) {
    updateDhcpDns();  // new DNS from the network
    setSharing(true);
  } else if (id == IP_EVENT_PPP_LOST_IP) {
    setSharing(false);
  }
}

}  // namespace

void begin() {
  if (s_inited) return;
  s_ap = esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) {
    LOG("HOTSPOT", "Wi-Fi init failed");
    return;
  }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);  // our settings live in NVS already: no extra flash writes
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, nullptr);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, onIpEvent, nullptr);
  s_inited = true;
}

bool start() {
  if (!s_inited) begin();
  if (!s_inited) return false;
  if (s_on) return true;
  Settings& cfg = settings::get();
  wifi_config_t wc = {};
  txt::copy((char*)wc.ap.ssid, cfg.hotspotSsid, sizeof wc.ap.ssid);
  wc.ap.ssid_len = (uint8_t)strlen((char*)wc.ap.ssid);
  txt::copy((char*)wc.ap.password, cfg.hotspotPass, sizeof wc.ap.password);
  size_t passLen = strlen(cfg.hotspotPass);
  if (passLen < 8) {
    txt::copy(s_lastError, "password must have 8+ characters", sizeof s_lastError);
    LOG("HOTSPOT", "not started: %s", s_lastError);
    return false;
  }
  wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
  wc.ap.channel = HOTSPOT_CHANNEL;
  wc.ap.max_connection = cfg.hotspotMaxClients < 1 ? 1 : cfg.hotspotMaxClients > 8 ? 8 : cfg.hotspotMaxClients;
  wc.ap.pmf_cfg.capable = true;
  wc.ap.pmf_cfg.required = false;
  esp_err_t e = esp_wifi_set_mode(WIFI_MODE_AP);
  if (e == ESP_OK) e = esp_wifi_set_config(WIFI_IF_AP, &wc);
  if (e == ESP_OK) e = esp_wifi_start();
  if (e != ESP_OK) {
    snprintf(s_lastError, sizeof s_lastError, "Wi-Fi start failed (%s)", esp_err_to_name(e));
    LOG("HOTSPOT", "%s", s_lastError);
    return false;
  }
  s_on = true;
  s_clients = 0;
  s_upSince = plogMillis();
  s_lastError[0] = 0;
  LOG("HOTSPOT", "Started \"%s\" (WPA2, channel %d, up to %d devices)", cfg.hotspotSsid, HOTSPOT_CHANNEL,
      wc.ap.max_connection);
  events::post(Ev::HotspotChanged, 0);
  return true;
}

void stop() {
  if (!s_on) return;
  esp_netif_napt_disable(s_ap);
  esp_wifi_stop();
  s_on = false;
  s_sharing = false;
  s_clients = 0;
  LOG("HOTSPOT", "Stopped");
  events::post(Ev::HotspotChanged, 0);
}

bool running() { return s_on; }

Status status() {
  Status s;
  s.on = s_on;
  s.sharing = s_sharing && s_on;
  s.clients = s_clients;
  txt::copy(s.ssid, settings::get().hotspotSsid, sizeof s.ssid);
  if (s_on && s_ap) {
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_ap, &ip) == ESP_OK) snprintf(s.ip, sizeof s.ip, IPSTR, IP2STR(&ip.ip));
  }
  s.upSinceMs = s_upSince;
  portENTER_CRITICAL(&s_mux);
  txt::copy(s.lastError, s_lastError, sizeof s.lastError);
  portEXIT_CRITICAL(&s_mux);
  return s;
}

int clients(Client* out, int max) {
  if (!s_on) return 0;
  wifi_sta_list_t list = {};
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return 0;
  int n = 0;
  for (int i = 0; i < list.num && n < max; ++i, ++n) {
    memcpy(out[n].mac, list.sta[i].mac, 6);
    out[n].rssi = list.sta[i].rssi;
  }
  return n;
}

void applySettings() {
  if (!s_on) return;
  stop();
  start();
}

}  // namespace hotspot
