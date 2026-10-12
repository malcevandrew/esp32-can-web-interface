#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <string>
#include <map>

// Простая структура для хранения физического статуса привода
struct DeviceStatus {
    std::string state = "UNHOMED"; // Начальный статус по умолчанию
    int32_t position = 0;          // Текущая координата в мм
};

class DeviceManager {
public:
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    static DeviceManager& getInstance();
    
    void init();
    void executeCommand(const std::string& device_name, const std::string& command, int32_t val);
    
    // Метод формирования JSON телеметрии для веб-сервера
    std::string getSystemTelemetryJson();

    // Коллбек для обновления данных из CAN-шины
    static void handleIncomingCanFrame(uint8_t state, int32_t position);

private:
    DeviceManager() = default;
    ~DeviceManager() = default;
    
    // РЕШЕНИЕ: Карта хранит чистые структуры данных реального железа!
    std::map<std::string, DeviceStatus> _devices;
};

#endif // DEVICE_MANAGER_H
