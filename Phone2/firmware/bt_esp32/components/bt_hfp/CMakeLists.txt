idf_component_register(
  SRCS "hfp_ag.cpp"
  INCLUDE_DIRS "include"
  REQUIRES bt btlink core
  PRIV_REQUIRES audio nvs_flash
)
