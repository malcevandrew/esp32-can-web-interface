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

const char* MY_SSID = "netis";
const char* MY_PASS = "paswwordd";

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Инициализируем менеджер устройств (создаем виртуальные узлы)
    DeviceManager::getInstance().init();

    // Инициализируем аппаратный драйвер TWAI (CAN) на скорости 500 кбит/с
    CanMaster::getInstance().init();

    // 2. Старт Wi-Fi
    WiFiManager::getInstance().init(MY_SSID, MY_PASS);

    bool server_started = false;


    while (1) {

        if (WiFiManager::getInstance().isConnected()) {
            if (!server_started) {
                WebServer::getInstance().start();
                server_started = true;
            }
        }
        
        // Минимальный квант сна FreeRTOS (1 тик = 10 мс по дефолту), чтобы дать работать другим задачам
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
