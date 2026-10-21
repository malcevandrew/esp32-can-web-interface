#include "WiFiManager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_mgr";
#define WIFI_MAXIMUM_RETRY  5

WiFiManager& WiFiManager::getInstance() {
    static WiFiManager instance;
    return instance;
}

void WiFiManager::init(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;

    ESP_LOGI(TAG, "Initializing Wi-Fi subsystem...");

    // 1. Инициализация сетевого интерфейса (LwIP стек)
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 2. Создание дефолтного системного Event Loop, если он еще не создан
    esp_err_t err = esp_event_loop_create_default();
    
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    // 3. Создание Wi-Fi станции в сетевом стеке
    esp_netif_create_default_wifi_sta();

    // 4. Инициализация Wi-Fi с дефолтной конфигурацией распределения памяти
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 5. Регистрируем наш обработчик на любые события Wi-Fi и получение IP адреса
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WiFiManager::eventHandler,
                                                        this,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WiFiManager::eventHandler,
                                                        this,
                                                        nullptr));

    // 6. Заполнение конфигурационной структуры Wi-Fi
    wifi_config_t wifi_config = {};
    strlcpy((char*)wifi_config.sta.ssid, _ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, _password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // Базовый стандарт безопасности

    // 7. Запуск модуля
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi subsystem started. Awaiting event loop responses.");
}

bool WiFiManager::isConnected() const { // const гарантирует, что этот метод является read-only
    return _isConnected;
}

void WiFiManager::eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) 
{
    // Восстанавливаем безликий указатель на экземпляр класса из аргумента коллбека
    WiFiManager* manager = static_cast<WiFiManager*>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to AP...");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        
        manager->_isConnected = false;

        if (manager->_retry_cnt < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            manager->_retry_cnt++;
            ESP_LOGW(TAG, "Connection lost. Retry to connect to the AP (%d/%d)", manager->_retry_cnt, WIFI_MAXIMUM_RETRY);
        } else {
            ESP_LOGE(TAG, "Failed to connect to the AP. Max retries reached.");
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        manager->_isConnected = true;
        manager->_retry_cnt = 0;
        
        // Преобразуем IP-адрес в читаемый вид и выводим в лог
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "SUCCESS! Connected to AP. Assigned IP: %s", ip_str);
    }
}
