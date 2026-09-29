# ILI9341 panel + XPT2046 touch controller (LovyanGFX), PSRAM frame buffer, backlight.
idf_component_register(
  SRCS "display.cpp"
  INCLUDE_DIRS "include"
  REQUIRES LovyanGFX
  PRIV_REQUIRES board core heap
)
