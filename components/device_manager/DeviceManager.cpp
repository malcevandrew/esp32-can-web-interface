#include "DeviceManager.h"
#include "CanMaster.h"
#include "WebServer.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "device_manager";

// Обращение к единственному экземпляру класса DeviceManager
DeviceManager& DeviceManager::getInstance() {
    static DeviceManager instance;
    return instance;
}

// Инициализируем менеджер подключеных устройств (создаем виртуальные узлы)
void DeviceManager::init() {

    // Передаем указатель на функцию-обработчик принятого кадра CAN
    CanMaster::getInstance().registerExtendedCallback(DeviceManager::handleIncomingCanFrame);       

    // Инициализация наших узлов начальными значениями state = "UNHOMED" и position = 0
    _devices["stepper"] = DeviceStatus();
    _devices["actuator"] = DeviceStatus();

    // Фиксируем получения последнего кадра по CAN
    _last_received_packet_tick = xTaskGetTickCount();  

    // Запуск фонового супервизора для проверки, что шина связи не оборвана
    xTaskCreatePinnedToCore(
        DeviceManager::linkSupervisorTask,
        "link_supervisor",
        3072,
        nullptr,
        3,             // Средний приоритет
        nullptr,
        1              // На Ядре 1 вместе с Wi-Fi
    );

    ESP_LOGI(TAG, "Device Manager active");
}


// Запуск перемещения каретки на координату в ручном режиме
void DeviceManager::executeCommand(const std::string& device_name, const std::string& command, int32_t val) {
    if (device_name == "stepper") {
        // Если каретка уже запущена в режиме "Последовательность", то игнорируем выполнение ручных команд, кроме "stop"
        if (command == "goto") {
            if (_is_program_running) return;                        
            _stepper_target_mm = val;                               // Обновляем целевую позицию
            CanMaster::getInstance().sendNodeCommand(1, 0x02, val); // Передаем на упаковку и отправку 
        } else if (command == "home") {
            if (_is_program_running) return;
            _stepper_target_mm = 0;
            CanMaster::getInstance().sendNodeCommand(1, 0x01, 0);   
        } else if (command == "stop") {
            stopProgramSequence();                                  // При команде stop сбрасываем флаг ожидания ответа о завершении текущей операции
            CanMaster::getInstance().sendNodeCommand(1, 0x03, 0);        
        }
    }
}


// Инициализация старта движения каретки в режиме "Последовательность"
void DeviceManager::startProgramSequence(const std::vector<int32_t>& points) {
    if (points.empty()) return;
    
    _program_points = points;
    _current_program_index = 0;
    _waiting_for_done = false;
    _is_program_running = true;
    
    ESP_LOGW(TAG, "=== SEQUENCE STARTED: %d points loaded ===", points.size());
    
    executeNextProgramStep();   // Переходим к выполнению последовательности
}


// При команде "Stop" или в случае сбоя больше не ждем ответ "DONE"
void DeviceManager::stopProgramSequence() {
    _is_program_running = false;
    _waiting_for_done = false;
    ESP_LOGE(TAG, "=== SEQUENCE FORCED TO STOP ===");
}


// Переключатель шагов в режиме "Последовательность"
void DeviceManager::executeNextProgramStep() {
    if (!_is_program_running) return;

    // Если вся программа успешно выполнена, сбрасываем флаги
    if (_current_program_index >= _program_points.size()) {
        _is_program_running = false;
        _waiting_for_done = false;

        ESP_LOGW(TAG, "=== SEQUENCE SUCCESSFULLY COMPLETED ===");
        WebServer::getInstance().broadcastWsMessage(getSystemTelemetryJson());  // Обновляем показатели на веб-интерфейсе

        return;
    }

    // Обновляем цель
    int32_t next_target_mm = _program_points[_current_program_index];   
    _stepper_target_mm = next_target_mm;                                

    ESP_LOGW(TAG, "[AUTO CYCLE] Step %d/%d ➔ Sending GOTO %d mm", 
             _current_program_index + 1, _program_points.size(), next_target_mm);

    // Отправляем по CAN и получаем токен транзакции
    _active_transaction_token = CanMaster::getInstance().sendNodeCommand(1, 0x02, next_target_mm);
    
    if (_active_transaction_token > 0) {
        _waiting_for_done = true;                       // Ожидаем до следующего DONE
        _transaction_start_tick = xTaskGetTickCount();  // Фиксируем время старта транзакции на случай критической ситуации
    } else {
        stopProgramSequence();                          // Если буфер CAN забит, аварийно гасим цикл
    }
    
    WebServer::getInstance().broadcastWsMessage(getSystemTelemetryJson());  // Обновляем показатели на веб-интерфейсе
}


//  Асинхронный коллбек. Обработка принятого кадра CAN
void DeviceManager::handleIncomingCanFrame(uint8_t node_id, uint8_t cmd_id, uint8_t tx_status, uint8_t state, int32_t pos) {
    auto& manager = DeviceManager::getInstance();

    manager._last_received_packet_tick = xTaskGetTickCount();   // Фиксируем время получения последнего кадра по CAN
    
    // Расшифровываем байт текущего состояния
    std::string hardware_state;
    switch (state) {
        case 0x01: hardware_state = "MOVING"; break;
        case 0x02: hardware_state = "READY";  break;
        case 0x03: hardware_state = "FAULT";  break;
        case 0x04: hardware_state = "UNHOMED"; break;
        default:   hardware_state = "UNKNOWN"; break;
    }

    // Обновляем значения состояния и позиции
    if (node_id == 1) {
        manager._devices["stepper"].state = hardware_state;
        manager._devices["stepper"].position = pos;
    }

    //Защита, если посреди выполнения шага в режиме "последовательность" прилетел статус UNHOMED или FAULT
    if (manager._is_program_running && node_id == 1) {
        if (state == 0x04 || state == 0x03) { 
            ESP_LOGE(TAG, "[HOT REBOOT / FAULT DETECTED] Stepper lost its home or went to fault status!");

            manager.stopProgramSequence();                                                      // Больше не ждем ответ "DONE"
            WebServer::getInstance().broadcastWsMessage(manager.getSystemTelemetryJson());      // Обновляем показатели на веб-интерфейсе
            return;
        }
    }

    // Проверка статуса ответа транзации
    if (manager._is_program_running && node_id == 1 && manager._waiting_for_done) {
        if (cmd_id == manager._active_transaction_token) {                              // Если номер токена команды и ответа совпадают
            if (tx_status == 0xFF) {                                                    // Если NACK (отказ STM32 в выполнения команды)
                ESP_LOGE(TAG, "[CAN NACK] Execution rejected by STM32!");
                manager.stopProgramSequence();
            }
            else if (tx_status == 0x55) {           // Если пришел DONE
                manager._waiting_for_done = false;  // Снимаем блокировку транзакции
                manager._current_program_index++;   // Инкрементируем индекс
                
                
                manager.executeNextProgramStep();   // Вызываем выполнение следующего шага последовательности. Программа шагает вперед со скоростью физического ответа от STM32.

                return;                             // Выходим, так как executeNextProgramStep сам обновит WebSocket 
            }
        }
    }

    WebServer::getInstance().broadcastWsMessage(manager.getSystemTelemetryJson());  // Обновляем показатели на веб-интерфейсе
}


// Фоновый супервизор для проверки, что шина связи не оборвана (FreeRTOS Task)
void DeviceManager::linkSupervisorTask(void* pvParameters) {
    auto& manager = DeviceManager::getInstance();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Проверка каждые 100 мс

        // 1) Проверка на полный обрыв провода.
        // STM32 настроена отсылать кадры телеметрии каждые 50 мс
        if ((xTaskGetTickCount() - manager._last_received_packet_tick) > pdMS_TO_TICKS(350)) {  // Если от STM32 нет кадров дольше 350 мс,
            if (manager._devices["stepper"].state != "LINK_ERROR") {                            // то значит обрыв связи
                ESP_LOGE(TAG, "[HEARTBEAT CRITICAL] CAN Bus disconnected!");
                
                manager._devices["stepper"].state = "LINK_ERROR";
                if (manager._is_program_running) {                                              
                    manager.stopProgramSequence();                                              // Больше не ждем ответ "DONE"
                }
                WebServer::getInstance().broadcastWsMessage(manager.getSystemTelemetryJson());  // Обновляем показатели на веб-интерфейсе   
            }
        }

        // 2) Детекция рассинхронизации токенов.
        // Если запущен режим "Последовательность", но ответа DONE нет дольше чем 4 секунды после окончания шага
        if (manager._is_program_running && manager._waiting_for_done) {    
            
            // Если STM32 присылает статус "MOVING", мы постоянно сдвигаем метку времени старта транзакции вперед.
            // Тайм-аут произойдет только тогда, когда мотор физически остановится, но не пришлет DONE

            // Если мотор еще не закончил движение, то продлеваем время отсчитанное от момента отправки транзакции
            if (manager._devices["stepper"].state == "MOVING") {
                manager._transaction_start_tick = xTaskGetTickCount(); 
            }
            
            // Если мотор уже остановился (READY/FAULT/UNHOMED), но DONE так и не прилетел в течение 4 секунд
            if ((xTaskGetTickCount() - manager._transaction_start_tick) > pdMS_TO_TICKS(4000)) {
                
                ESP_LOGE(TAG, "[TOKEN DESYNC CRITICAL] Transaction token mismatch timeout! Active token: %d", manager._active_transaction_token);
                
                manager._devices["stepper"].state = "TRANSACTION_DESYNC";
                manager.stopProgramSequence();                                                  // Больше не ждем ответ "DONE"
                
                WebServer::getInstance().broadcastWsMessage(manager.getSystemTelemetryJson());  // Обновляем показатели на веб-интерфейсе
            }
        }
    }
}


// Собираем cJSON строку для отправки в браузер
std::string DeviceManager::getSystemTelemetryJson() {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddBoolToObject(root, "program_running", _is_program_running);
    cJSON_AddNumberToObject(root, "program_step", _current_program_index + 1);
    cJSON_AddNumberToObject(root, "program_total", _program_points.size());

    for (auto const& [name, status] : _devices) {
        cJSON *dev_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(dev_obj, "state", status.state.c_str());
        cJSON_AddNumberToObject(dev_obj, "position", status.position);
        cJSON_AddNumberToObject(dev_obj, "target", (name == "stepper") ? _stepper_target_mm : 0);
        cJSON_AddItemToObject(root, name.c_str(), dev_obj);
    }
    
    char *rendered = cJSON_PrintUnformatted(root);
    std::string result(rendered);
    cJSON_Delete(root);
    free(rendered);
    
    return result;
}