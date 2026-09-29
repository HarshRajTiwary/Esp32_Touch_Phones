idf_component_register(
  SRCS "app_main.cpp"
  INCLUDE_DIRS "."
  REQUIRES board core phonecore phone net nvs_flash esp_netif esp_event espcoredump
)
