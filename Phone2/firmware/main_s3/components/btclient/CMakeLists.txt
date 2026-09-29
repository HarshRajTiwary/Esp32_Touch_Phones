# S3 side of the BT link: mirrors the ESP32 helper's Bluetooth/HFP/audio state.
idf_component_register(
  SRCS "bt_client.cpp"
  INCLUDE_DIRS "include"
  REQUIRES core phonecore
  PRIV_REQUIRES btlink board modem esp_driver_gpio
)
