# Shared S3 types (phone_types.h), the event bus and the S3 timing constants.
idf_component_register(
  SRCS "events.cpp" "net_rules.cpp"
  INCLUDE_DIRS "include"
  REQUIRES core freertos
)
