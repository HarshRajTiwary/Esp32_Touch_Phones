# Phone2 – Wi-Fi hotspot

> UNVERIFIED: the hotspot compiles and follows Espressif's documented NAPT setup, but
> it has not been tested with a laptop yet. Do not rely on it until TEST-015 passes.

## 1. How it works

```
Airtel 4G ─► A7672S ─UART/CMUX ch.2─► PPP (esp_netif, default route)
                                          │  NAPT (lwIP IPv4 NAT)
laptop ◄── Wi-Fi WPA2 ── ESP32-S3 SoftAP 192.168.4.1 ── DHCP 192.168.4.x
```

* The ESP32-S3's own radio runs the SoftAP. Bluetooth is on the separate ESP32
  helper, so an earbud call and the hotspot do not share airtime.
* `esp_netif_napt_enable()` on the AP interface translates the laptop's traffic onto
  the PPP link (`CONFIG_LWIP_IPV4_NAPT`, `CONFIG_LWIP_IP_FORWARD`).
* DHCP gives clients the **mobile network's DNS server** (updated on every new PPP
  session). Before the first PPP session a public resolver (8.8.8.8) is offered.
* Wi-Fi settings are kept in RAM only (`WIFI_STORAGE_RAM`) – the phone's own
  settings in NVS are the single source.

Code: `firmware/main_s3/components/net/hotspot.cpp`, UI `ui/screens_net.cpp`.

## 2. Settings (Home → Hotspot)

| Setting | Default | Rules |
|---|---|---|
| Name (SSID) | `Phone2-XXXX` (from the MAC address, generated at first boot) | 1–32 characters |
| Password | 10 random characters generated at first boot (never a password in the source code) | 8–63 characters (WPA2) |
| Security | WPA2-PSK, PMF capable | – |
| Max devices | 4 | 1–8 |
| Turn on at start-up | off | – |
| Channel | 6 (`HOTSPOT_CHANNEL` in `s3_config.h`) | – |

The screen shows ON/OFF, "Sharing mobile data" / "Waiting for mobile data", the
network name, the password (hidden, "Show" on request), connected devices, the IP
address and data used (down/up since boot). Mobile data must be on
(Home → Mobile data).

## 3. Performance (expected, UNVERIFIED)

The bottleneck is the modem UART, not Wi-Fi or LTE:

| Modem UART | Hotspot speed |
|---|---|
| 115200 baud (default) | ≈ 85 kbit/s – messaging, e-mail text, light browsing |
| 921600 baud | ≈ 650 kbit/s – browsing, low-quality video |
| 3 Mbaud + RTS/CTS | ≈ 2 Mbit/s |
| USB host link (future) | several Mbit/s |

See [MODEM.md §2](MODEM.md#2-serial-link-one-uart-two-channels) for what the level
shifter on the carrier board allows.

## 4. Recovery

* If 4G drops, the hotspot stays on and shows "Waiting for mobile data"; the data task
  reconnects (5 s → 2 min back-off) and sharing resumes by itself with the new DNS.
* Short registration losses (< 15 s, e.g. a 2G voice call) do not drop PPP.
* Calls, SMS, Bluetooth and the UI never wait for the hotspot or the data link – they
  run in other tasks and on another CMUX channel.

## 5. Test with a laptop (TEST-015)

1. Mobile data shows **Connected** with an IP (Settings → Mobile data → Test internet
   connection → "Internet OK").
2. Hotspot → Turn ON. The status bar shows the Wi-Fi symbol.
3. Laptop: join `Phone2-XXXX` with the shown password. Expected: IP `192.168.4.x`,
   gateway `192.168.4.1`.
4. `ping 8.8.8.8`, then open a web page. Hotspot shows "1 device", data counters rise.
5. During the session: make and receive a call – the hotspot must keep working
   (possibly slower on 2G fall-back) and the call must be unaffected.
6. Pull the antenna for 30 s, put it back: the laptop regains internet without
   touching the phone.
