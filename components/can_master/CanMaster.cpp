#include "CanMaster.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" 
#include "esp_log.h"
#include <string.h>

static const char *TAG = "can_master";

#define CAN_TX_PIN GPIO_NUM_17  
#define CAN_RX_PIN GPIO_NUM_16  

CanMaster& CanMaster::getInstance() {
    static CanMaster instance;
    return instance;
}

static void can_rx_task(void *arg) {
    twai_message_t rx_msg;
    while (1) {
        if (twai_receive(&rx_msg, portMAX_DELAY) == ESP_OK) {
            // Если прилетел пакет от STM32 (ID = 0x300)
            if (rx_msg.identifier == 0x300 && rx_msg.data_length_code == 5) {
                uint8_t state_code = rx_msg.data[0];
                
                // Сборка 32-битного числа из 4 байт (Big Endian)
                int32_t real_pos = ((int32_t)rx_msg.data[1] << 24) |
                                   ((int32_t)rx_msg.data[2] << 16) |
                                   ((int32_t)rx_msg.data[3] << 8)  |
                                   ((int32_t)rx_msg.data[4]);
                
                // Вызываем коллбек и передаем данные «в воздух» подписчику
                CanMaster::getInstance().triggerCallback(state_code, real_pos);
            }
        }
    }
}

void CanMaster::init() {
    ESP_LOGI(TAG, "Initializing TWAI (CAN) in NORMAL mode...");

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            _is_initialized = true;
            ESP_LOGI(TAG, "TWAI Controller started in NORMAL mode.");
            
            // Запускаем фоновую задачу прослушивания шины
            xTaskCreatePinnedToCore(can_rx_task, "can_rx_task", 3072, nullptr, 6, nullptr, 1);
        } else {
            ESP_LOGE(TAG, "Failed to start TWAI!");
        }
    } else {
        ESP_LOGE(TAG, "Failed to install TWAI driver!");
    }
}

void CanMaster::sendStepperGoto(int32_t position) {
    if (!_is_initialized) return;

    twai_message_t tx_msg;
    memset(&tx_msg, 0, sizeof(twai_message_t));
    tx_msg.identifier = 0x200;      
    tx_msg.data_length_code = 5;    
    tx_msg.data[0] = 0x02;          
    
    tx_msg.data[1] = (uint8_t)((position >> 24) & 0xFF);
    tx_msg.data[2] = (uint8_t)((position >> 16) & 0xFF);
    tx_msg.data[3] = (uint8_t)((position >> 8)  & 0xFF);
    tx_msg.data[4] = (uint8_t)(position         & 0xFF);

    twai_transmit(&tx_msg, pdMS_TO_TICKS(10));
}

void CanMaster::sendStepperHome() {
    if (!_is_initialized) return;

    twai_message_t tx_msg;
    memset(&tx_msg, 0, sizeof(twai_message_t));
    tx_msg.identifier = 0x200;
    tx_msg.data_length_code = 1;    
    tx_msg.data[0] = 0x01;          

    twai_transmit(&tx_msg, pdMS_TO_TICKS(10));
}


void CanMaster::sendStepperStop() {
    if (!_is_initialized) return;

    twai_message_t tx_msg;
    memset(&tx_msg, 0, sizeof(twai_message_t));
    
    tx_msg.identifier = 0x200;       // Целевой ID нашего шаговика
    tx_msg.data_length_code = 1;     // Передаем только 1 байт команды
    tx_msg.data[0] = 0x03;           // Промышленный код операции: 0x03 = STOP

    // Выстреливаем пакет в шину данных с минимальным таймаутом
    if (twai_transmit(&tx_msg, pdMS_TO_TICKS(5)) == ESP_OK) {
        ESP_LOGW(TAG, "[CAN TX] EMERGENCY STOP sent to node 0x200");
    } else {
        ESP_LOGE(TAG, "[CAN TX] Failed to queue STOP frame!");
    }
}