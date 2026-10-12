#include "DeviceManager.h"
#include "CanMaster.h"
#include "WebServer.h" // ИСПРАВЛЕНО: Подключаем сервер для мгновенной отправки
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "device_manager";

DeviceManager& DeviceManager::getInstance() {
    static DeviceManager instance;
    return instance;
}

void DeviceManager::init() {
    // Регистрируем коллбек на прием кадров из шины CAN
    CanMaster::getInstance().registerCallback(DeviceManager::handleIncomingCanFrame);

    _devices["stepper"] = DeviceStatus();
    _devices["actuator"] = DeviceStatus();
    
    ESP_LOGI(TAG, "Hardware Device Manager initialized.");
}

void DeviceManager::executeCommand(const std::string& device_name, const std::string& command, int32_t val) {
    if (device_name == "stepper") {
        if (command == "goto") {
            CanMaster::getInstance().sendStepperGoto(val);
        } else if (command == "home") {
            CanMaster::getInstance().sendStepperHome();
        } else if (command == "stop") {
            CanMaster::getInstance().sendStepperStop();
        }
    }
}

// ЭТА ФУНКЦИЯ СРАБАТЫВАЕТ АСИНХРОННО КАЖДЫЕ 50 МС (По прерыванию от CAN-шины!)
void DeviceManager::handleIncomingCanFrame(uint8_t state, int32_t position) {
    std::string hardware_state;
    switch (state) {
        case 0x01: hardware_state = "MOVING"; break;
        case 0x02: hardware_state = "READY";  break;
        case 0x03: hardware_state = "FAULT";  break;
        default:   hardware_state = "UNKNOWN"; break;
    }
    
    auto& manager = DeviceManager::getInstance();
    if (manager._devices.find("stepper") != manager._devices.end()) {
        // 1. Записываем свежие данные в структуру памяти
        manager._devices["stepper"].state = hardware_state;
        manager._devices["stepper"].position = position;
        
        // 2. РЕШЕНИЕ: Мгновенно генерируем JSON-строку телеметрии
        std::string telemetry = manager.getSystemTelemetryJson();
        
        // 3. РЕШЕНИЕ: Сразу выстреливаем её в WebSocket-канал! 
        // Данные летят в браузер со скоростью физического обновления МК STM32.
        WebServer::getInstance().broadcastWsMessage(telemetry);
    }
}

std::string DeviceManager::getSystemTelemetryJson() {
    cJSON *root = cJSON_CreateObject();
    
    for (auto const& [name, status] : _devices) {
        cJSON *dev_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(dev_obj, "state", status.state.c_str());
        cJSON_AddNumberToObject(dev_obj, "position", status.position);
        cJSON_AddItemToObject(root, name.c_str(), dev_obj);
    }
    
    char *rendered = cJSON_PrintUnformatted(root);
    std::string result(rendered);
    
    cJSON_Delete(root);
    free(rendered);
    
    return result;
}
