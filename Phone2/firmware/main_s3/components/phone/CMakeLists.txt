# Application core: event handling, call behaviour, audio routing, notifications.
idf_component_register(
  SRCS "phone_app.cpp" "battery.cpp" "ringer.cpp"
  INCLUDE_DIRS "include"
  REQUIRES btclient storage touch phonecore core
  PRIV_REQUIRES ui diag modem net display board esp_driver_ledc heap
)
