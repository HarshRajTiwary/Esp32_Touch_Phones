# BT link: protocol + framing (pure, host-tested) and the UART transport.
idf_component_register(
  SRCS "btlink_codec.cpp" "btlink_uart.cpp" "sco_policy.cpp"
  INCLUDE_DIRS "include"
  REQUIRES core esp_driver_uart esp_driver_gpio freertos
)
