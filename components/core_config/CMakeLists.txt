idf_component_register(
    SRCS "core_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash driver core_rtos
)
