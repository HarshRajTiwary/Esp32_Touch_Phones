# Contacts, call history, SMS store (LittleFS) and settings (NVS).
idf_component_register(
  SRCS "storage.cpp" "settings.cpp" "contacts.cpp" "call_log.cpp" "messages.cpp"
  INCLUDE_DIRS "include"
  REQUIRES phonecore core
  PRIV_REQUIRES esp_littlefs nvs_flash esp_hw_support heap
)
