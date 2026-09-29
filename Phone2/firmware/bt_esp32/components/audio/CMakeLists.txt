idf_component_register(
  SRCS "audio_bridge.cpp" "dsp.cpp"
  INCLUDE_DIRS "include"
  REQUIRES btlink core
  PRIV_REQUIRES bt board esp_adc esp_driver_dac esp_driver_gptimer hal
)
