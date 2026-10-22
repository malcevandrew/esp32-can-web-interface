#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_wifi.h"
#include "esp_event.h"

class WiFiManager {
public:

    // Удаляем конструкторы копирования и присваивания (запрещаем дублирование)
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;

    // Функция доступа к единственному экземпляру класса WiFiManager
    static WiFiManager& getInstance();  

    // Инициализация и запуск подключения
    void init(const char* ssid, const char* password);
    
    // Проверка статуса подключения
    bool isConnected() const;

private:
    WiFiManager() = default;
    ~WiFiManager() = default;

    // Статический Си-коллбек, который требует ESP-IDF Event Loop
    static void eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    const char* _ssid = nullptr;
    const char* _password = nullptr;
    bool _isConnected = false;
    uint8_t _retry_cnt = 0;
};

#endif // WIFI_MANAGER_H
