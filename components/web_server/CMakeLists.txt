idf_component_register(SRCS "WebServer.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_server spiffs vfs device_manager)
