#include "CanMaster.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "can_master";

// Гарантирует, что будет существовать только один экземпляр класса
CanMaster& CanMaster::getInstance() {
    static CanMaster instance;
    return instance;
}


// Инициализация аппаратного драйвера TWAI (CAN) 
esp_err_t CanMaster::init() {
    if (_is_initialized) return ESP_OK;

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)17, (gpio_num_t)16, TWAI_MODE_NORMAL); // gpio17 - СTX, gpio16 - СRX
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
        return ESP_FAIL;
    }

    if (twai_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver");
        return ESP_FAIL;
    }

    _is_initialized = true;

    // Запускаем асинхронную задачу прослушивания сети на Ядре 0
    xTaskCreatePinnedToCore(can_rx_task, "can_rx_task", 3584, nullptr, 5, nullptr, 0);

    ESP_LOGI(TAG, "TWAI driver successfully initialized.");
    return ESP_OK;
}

// Упаковщик команд транзакции. Возвращает номер этой транзакции
uint8_t CanMaster::sendNodeCommand(uint8_t node_id, uint8_t cmd_code, int32_t target_mm) {
    if (!_is_initialized) return 0;

    // Инкрементируем и генерируем уникальный ID для этой транзакции (Token)
    if (++_global_transaction_counter == 0) _global_transaction_counter = 1;
    uint8_t new_token = _global_transaction_counter;

    twai_message_t tx_msg;
    memset(&tx_msg, 0, sizeof(twai_message_t)); //заполняет каждый байт структуры нулями
    
    // Создание адреса: ID кадра = 0x200 + Node ID        
    tx_msg.identifier = 0x200 + node_id;            // у шаговика Node ID = 1, т.е. адрес 0x201
    tx_msg.data_length_code = 6;                    // Длина пакета

    tx_msg.data[0] = new_token;                     // Байт 0: Токен транзакции
    tx_msg.data[1] = cmd_code;                      // Байт 1: Код операции (0x01/0x02/0x03)

    // Координата в формате Big Endian
    tx_msg.data[2] = (target_mm >> 24) & 0xFF;
    tx_msg.data[3] = (target_mm >> 16) & 0xFF;
    tx_msg.data[4] = (target_mm >> 8)  & 0xFF;
    tx_msg.data[5] = target_mm         & 0xFF;

    if (twai_transmit(&tx_msg, pdMS_TO_TICKS(5)) == ESP_OK) {
        ESP_LOGI(TAG, "[CAN TX] Sent Trans: Token=%d, Cmd=0x%02X, Node=0x%X", new_token, cmd_code, tx_msg.identifier);
    } else {
        ESP_LOGE(TAG, "[CAN TX] Buffer full! Failed to send token %d", new_token);
        return 0; // Ошибка отправки
    }

    return new_token; // Возвращаем токен, чтобы Менеджер устройств мог следить за этой задачей!
}

// Передаем указатель на функцию-обработчик
void CanMaster::registerExtendedCallback(CanExtendedCallback cb) {
    _extended_callback = cb;
}

// Переход внутрь метода DeviceManager::handleIncomingCanFrame
void CanMaster::triggerExtendedCallback(uint8_t node_id, uint8_t cmd_id, uint8_t tx_status, uint8_t state, int32_t pos) {
    if (_extended_callback) {
        _extended_callback(node_id, cmd_id, tx_status, state, pos);
    }
}

// Асинхронный прием и декодирование кадров CAN (FreeRTOS Task)
void CanMaster::can_rx_task(void* pvParameters) {
    twai_message_t rx_msg;
    ESP_LOGI(TAG, "CAN RX Receiver thread started.");

    while (true) {
        // Задача не загружает ядро процессора, пока в шине нет физических кадров 
        if (twai_receive(&rx_msg, portMAX_DELAY) == ESP_OK) {
            
            // Фильтруем только наш диапазон ответов (0x301 ... 0x30F)
            if (rx_msg.identifier >= 0x301 && rx_msg.identifier <= 0x30F && rx_msg.data_length_code == 7) {
                
                // РАСШИФРОВКА АДРЕСА: Вычисляем Node ID по заголовку кадра (0x301 -> Node 1) 
                uint8_t node_id = rx_msg.identifier - 0x300; 

                uint8_t echo_token = rx_msg.data[0];   // Байт 0: Токен транзакции
                uint8_t tx_status  = rx_msg.data[1];   // Байт 1: Квитанция (ACK/NACK/DONE)
                uint8_t state_code = rx_msg.data[2];   // Байт 2: Состояние автомата

                // Байт 3-6: Позиция в мм Big Endian
                int32_t real_pos = ((int32_t)rx_msg.data[3] << 24) |
                                   ((int32_t)rx_msg.data[4] << 16) |
                                   ((int32_t)rx_msg.data[5] << 8)  |
                                   ((int32_t)rx_msg.data[6]);

                // Передаем данные в DeviceManager
                CanMaster::getInstance().triggerExtendedCallback(node_id, echo_token, tx_status, state_code, real_pos);
            }
        }
    }
}