idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES wifi_manager web_server device_manager can_master nvs_flash)


# Актуальная функция для генерации бинарного образа раздела во Flash для ESP-IDF v6.x
spiffs_create_partition_image(storage "../storage" FLASH_IN_PROJECT)
