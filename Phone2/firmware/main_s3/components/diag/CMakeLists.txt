# USB serial debug console.
idf_component_register(
  SRCS "console.cpp"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES phone ui modem net btclient storage touch core esp_driver_uart heap
)
