idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "." "screens"
    EMBED_TXTFILES "web/index.html"
    REQUIRES core_rtos core_config core_peripherals core_ui core_portal
)
