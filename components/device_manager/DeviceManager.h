#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include "freertos/FreeRTOS.h"


// Структура для хранения физического статуса привода
struct DeviceStatus {
    std::string state = "UNHOMED"; // По умолчанию
    int32_t position = 0;          // В миллиметрах
};

class DeviceManager {
public:

    // Запрет на клонирование. Удаляем конструктор копирования и оператор присваивания
    DeviceManager(const DeviceManager&) = delete;               
    DeviceManager& operator=(const DeviceManager&) = delete;

    // Гарантирует, что будет существовать только один экземпляр класса
    static DeviceManager& getInstance();
    
    // Инициализируем менеджер подключеных устройств (создаем виртуальные узлы)
    void init();

    // Запуск перемещения каретки в ручном режиме
    void executeCommand(const std::string& device_name, const std::string& command, int32_t val);
    
    // Метод загрузки и запуска автоматической программы перемещений из веб-интерфейса
    void startProgramSequence(const std::vector<int32_t>& points);

    // ////////////////////////////////////////////////////
    void stopProgramSequence();
    
    // Переключатель шагов
    void executeNextProgramStep(); 

    // Собираем cJSON строку для отправки в браузер
    std::string getSystemTelemetryJson();

    // Асинхронный коллбек. Обработка принятого ответа по CAN
    static void handleIncomingCanFrame(uint8_t node_id, uint8_t cmd_id, uint8_t tx_status, uint8_t state, int32_t pos);

    // Фоновый супервизор связи для проверки, что шина связи не оборвана (FreeRTOS Task)
    static void linkSupervisorTask(void* pvParameters);

private:
    DeviceManager() = default;
    ~DeviceManager() = default;
    
    std::map<std::string, DeviceStatus> _devices;       // Карта для хранения названий устройств и соответствующих им физических статусов и положений

    // Перемещение каретки в режиме "последовательность"
    std::vector<int32_t> _program_points;               // Набор координат введенных оператором
    size_t _current_program_index = 0;                  // Текущий шаг в цепочке перемещений
    volatile bool _is_program_running = false;          // Запущена ли сейчас каретка в режиме "последовательность"?
    
    volatile uint8_t _active_transaction_token = 0;     // Токен текущей транзакции 
    volatile bool _waiting_for_done = false;            // Ожидаем ли сейчас, что должен прийти кадр об успешном выполнении команды (со статусом "done")
    TickType_t _last_command_sent_tick = 0; 
    
    int32_t _stepper_target_mm = 0;                     // Хранилище текущей целевой позиции для телеметрии

    volatile uint32_t _last_received_packet_tick = 0;   // Время фиксации получения последнего кадра по CAN

    uint32_t _transaction_start_tick = 0;               // Время старта текущего шага перемещения
};

#endif // DEVICE_MANAGER_H