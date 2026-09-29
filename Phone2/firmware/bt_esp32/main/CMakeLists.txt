idf_component_register(
  SRCS "app_main.cpp" "helper_console.cpp"
  INCLUDE_DIRS "."
  REQUIRES board core btlink bt_hfp audio nvs_flash esp_driver_uart
)
