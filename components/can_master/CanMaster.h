#ifndef CAN_MASTER_H
#define CAN_MASTER_H


#include "esp_err.h"
#include <functional>

// Универсальный коллбек для обработки кадров CAN. (Node_ID, Command_ID, Transaction_Status, State_Code, Position_MM)
using CanExtendedCallback = std::function<void(uint8_t, uint8_t, uint8_t, uint8_t, int32_t)>;

class CanMaster {
public:
    static CanMaster& getInstance();

    // Запрет на клонирование. Удаляем конструктор копирования и оператор присваивания
    CanMaster(const CanMaster&) = delete;            
    CanMaster& operator=(const CanMaster&) = delete;

    esp_err_t init();   // Инициализация аппаратного драйвера TWAI (CAN) 
    
    // Новые транзакционные методы управления узлами шины
    uint8_t sendNodeCommand(uint8_t node_id, uint8_t cmd_code, int32_t target_mm);
    
    // Для межмодульного взаимодействия.
    // В DeviceManager.срр мы назначаем функцию DeviceManager::handleIncomingCanFrame() обработчиком входящих по CAN кадров
    void registerExtendedCallback(CanExtendedCallback cb);
    void triggerExtendedCallback(uint8_t node_id, uint8_t cmd_id, uint8_t tx_status, uint8_t state, int32_t pos);

private:
    CanMaster() = default;
    ~CanMaster() = default;

    bool _is_initialized = false;                       // Драйвер CAN инициализирован успешно
    uint8_t _global_transaction_counter = 0;            // Инкрементируемый генератор токенов
    CanExtendedCallback _extended_callback = nullptr;   // Слот под коллбек

    static void can_rx_task(void* pvParameters);        // Асинхронный прием и декодирование кадров CAN (FreeRTOS Task)
};

#endif // CAN_MASTER_H