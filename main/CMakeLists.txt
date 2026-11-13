idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "." "screens"
    REQUIRES core_rtos core_config core_peripherals core_ui
)
