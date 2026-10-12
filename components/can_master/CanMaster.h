#ifndef CAN_MASTER_H
#define CAN_MASTER_H

#include <cstdint>

// Объявляем тип указателя на функцию обратного вызова (коллбек)
typedef void (*CanMsgCallback)(uint8_t state, int32_t position);

class CanMaster {
public:
    CanMaster(const CanMaster&) = delete;
    CanMaster& operator=(const CanMaster&) = delete;

    static CanMaster& getInstance();
    
    void init();
    void sendStepperGoto(int32_t position);
    void sendStepperHome();
    
    // Метод, через который DeviceManager сможет подписаться на данные
    void registerCallback(CanMsgCallback cb) { _rx_callback = cb; }

    // Метод для вызова коллбека из асинхронной задачи
    void triggerCallback(uint8_t state, int32_t position) {
        if (_rx_callback != nullptr) {
            _rx_callback(state, position);
        }
    }

    void sendStepperStop(); // Прототип функции экстренной остановки

private:
    CanMaster() = default;
    ~CanMaster() = default;
    
    bool _is_initialized = false;
    CanMsgCallback _rx_callback = nullptr; // Ссылка на функцию-подписчика
};

#endif // CAN_MASTER_H
