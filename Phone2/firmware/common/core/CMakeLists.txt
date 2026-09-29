# Shared helpers used by both MCUs: logging, text/UTF-8/number/time helpers,
# COBS + CRC framing, lock-free sample ring. Everything except plog's locking is
# plain C++ so tests/host can compile it on the PC.
idf_component_register(
  SRCS "plog.cpp" "text_util.cpp" "cobs.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_timer freertos
)
