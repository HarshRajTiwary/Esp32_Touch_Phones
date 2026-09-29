# Immediate-mode GUI toolkit (stylus friendly), icons, keyboards and every screen.
idf_component_register(
  SRCS "gui.cpp" "icons.cpp" "keyboard.cpp" "nav.cpp"
       "screens_main.cpp" "screens_phone.cpp" "screens_sms.cpp" "screens_contacts.cpp"
       "screens_settings.cpp" "screens_net.cpp" "screens_diag.cpp"
  INCLUDE_DIRS "include"
  REQUIRES display touch phonecore storage btclient LovyanGFX core
  PRIV_REQUIRES phone modem net board heap
)
