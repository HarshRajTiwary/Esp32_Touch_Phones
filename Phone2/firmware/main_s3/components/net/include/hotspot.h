#pragma once
// Wi-Fi hotspot: ESP32-S3 SoftAP + NAPT onto the 4G PPP link.
//
//   Airtel 4G -> A7672S -> PPP (default route) -> NAPT -> SoftAP 192.168.4.1 -> laptop
//
// The S3 has its own radio: the Bluetooth earbuds run on the separate ESP32
// helper, so hotspot and earbud calls do not share airtime.
#include <stdint.h>

#include "app_limits.h"

namespace hotspot {

struct Status {
  bool on = false;
  bool sharing = false;         // 4G link up and NAPT active
  int clients = 0;
  char ssid[WIFI_SSID_MAX] = "";
  char ip[16] = "";             // the phone's address on the hotspot network
  uint32_t upSinceMs = 0;
  char lastError[48] = "";
};

struct Client {
  uint8_t mac[6];
  int8_t rssi;
};

void begin();                   // Wi-Fi driver init (radio stays off)
bool start();                   // uses settings: name, password, max clients
void stop();
bool running();
Status status();
int clients(Client* out, int max);
// Re-applies name/password/max clients (restarts the AP if it is running).
void applySettings();

}  // namespace hotspot
