idf_component_register(
    SRCS
        "nextion_driver.cpp"
        "encoder_driver.cpp"
        "pwm_driver.cpp"
        "adc_driver.cpp"
        "wifi_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc esp_wifi esp_netif esp_event esp_timer core_rtos core_config
)
