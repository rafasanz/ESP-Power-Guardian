idf_component_register(
    SRCS
        "app_main.cpp"
        "guardian_state.cpp"
        "status_led.cpp"
        "nut_server.cpp"
        "settings.cpp"
        "wifi_manager.cpp"
        "web_server.cpp"
        "usb_monitor.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event nvs_flash lwip driver esp_http_server app_update usb
)
