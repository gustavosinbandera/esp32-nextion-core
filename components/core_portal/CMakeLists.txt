idf_component_register(
    SRCS "core_portal.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server lwip json driver esp_timer core_rtos core_config core_peripherals core_ui
)
