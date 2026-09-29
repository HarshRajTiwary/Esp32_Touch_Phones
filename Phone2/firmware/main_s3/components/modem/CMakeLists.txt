# CellularModem API (modem.h) + SIMCom A7672S driver, CMUX, PPP data, AT/SMS parsers.
idf_component_register(
  SRCS "a7672s.cpp" "a7672s_at.cpp" "a7672s_call.cpp" "call_tracker.cpp" "a7672s_sms.cpp" "a7672s_network.cpp"
       "a7672s_audio.cpp" "a7672s_data.cpp" "modem_port.cpp" "cmux.cpp"
       "at_parse.cpp" "sms_pdu.cpp" "sms_assembler.cpp"
  INCLUDE_DIRS "include"
  REQUIRES phonecore core
  PRIV_REQUIRES board esp_driver_uart esp_driver_gpio esp_netif esp_event lwip
)
