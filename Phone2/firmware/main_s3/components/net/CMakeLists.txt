# Wi-Fi hotspot (SoftAP + NAPT onto the 4G PPP link), SNTP, connectivity test.
idf_component_register(
  SRCS "hotspot.cpp" "nettools.cpp"
  INCLUDE_DIRS "include"
  REQUIRES core
  PRIV_REQUIRES phonecore modem storage esp_wifi esp_netif esp_event lwip esp_http_client
)
