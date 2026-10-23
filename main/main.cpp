#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "WiFiManager.h"
#include "WebServer.h"
#include "DeviceManager.h"
#include "CanMaster.h"

static const char *TAG = "main_app";

const char* MY_SSID = "wifi_ssid";     //логин и пароль wi-fi сети
const char* MY_PASS = "wifi_password";

extern "C" void app_main(void) {
    
    // Шаблон инициализации энергонезависимой памяти (NVS) во флеш-памяти ESP32
    esp_err_t ret = nvs_flash_init();    
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { // При любых программных сбоях памяти 
        ESP_ERROR_CHECK(nvs_flash_erase());                                         // контроллер сам очистит 
        ret = nvs_flash_init();                                                     // и запустится в стабильном рабочем состоянии
    }
    ESP_ERROR_CHECK(ret);

    // Инициализируем менеджер устройств (создаем виртуальные узлы)
    DeviceManager::getInstance().init();

    // Инициализируем аппаратный драйвер TWAI (CAN) на скорости 500 кбит/с
    CanMaster::getInstance().init();

    // Старт Wi-Fi
    WiFiManager::getInstance().init(MY_SSID, MY_PASS);
   
    bool server_started = false;

    while (1) {
        if (WiFiManager::getInstance().isConnected()) {
            if (!server_started) {
                WebServer::getInstance().start();
                server_started = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // На 20 мс разгружаем ядро FreeRTOS от этой задачи
    }
}
