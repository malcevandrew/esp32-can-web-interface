idf_component_register(SRCS "DeviceManager.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES cjson freertos esp_timer cxx can_master web_server)
