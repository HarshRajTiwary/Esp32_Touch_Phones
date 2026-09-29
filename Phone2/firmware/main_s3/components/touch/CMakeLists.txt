# Resistive touch: raw reading, debounce/smoothing, 4-point calibration (touch_cal.cpp is pure).
idf_component_register(
  SRCS "touch.cpp" "touch_cal.cpp"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES display board
)
