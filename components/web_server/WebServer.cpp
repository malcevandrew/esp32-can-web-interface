#include "WebServer.h"       // КРИТИЧЕСКИЙ ИНКЛУД! Возвращает контекст класса
#include "esp_http_server.h" // Этот у нас уже есть опосредованно
#include "DeviceManager.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h" 
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <vector>            // Подключаем вектор для сборки массива точек

static const char *TAG = "web_server";      // Для CMakeList.txt
#define FILE_BUFFER_SIZE 1024

WebServer& WebServer::getInstance() {
    static DeviceManager& manager = DeviceManager::getInstance(); // Гарантируем линк менеджера 
    static WebServer instance;
    return instance;
}

// Поднятие LittleFS
void WebServer::initLittleFS() {
    ESP_LOGI(TAG, "Mounting VFS Flash partition...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",      
        .partition_label = "storage", 
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount partition. Check filesystem image.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find partition label 'storage'.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS/LittleFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Partition mounted successfully at /storage");
}

// Запуск HTTP-сервера на порту 80
void WebServer::start() {
    initLittleFS();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true; 
    config.uri_match_fn = httpd_uri_match_wildcard; 

    ESP_LOGI(TAG, "Starting HTTP Server on port: %d", config.server_port);
    
    if (httpd_start(&_server_handle, &config) == ESP_OK) {
        registerRoutes();
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server!");
    }
}

// Регистрация маршрутов
void WebServer::registerRoutes() {
    httpd_uri_t index_uri;
    memset(&index_uri, 0, sizeof(httpd_uri_t));
    index_uri.uri = "/";
    index_uri.method = HTTP_GET;
    index_uri.handler = indexHandler;
    httpd_register_uri_handler(_server_handle, &index_uri);

    httpd_uri_t css_uri;
    memset(&css_uri, 0, sizeof(httpd_uri_t));
    css_uri.uri = "/style.css";
    css_uri.method = HTTP_GET;
    css_uri.handler = cssHandler;
    httpd_register_uri_handler(_server_handle, &css_uri);

    httpd_uri_t js_uri;
    memset(&js_uri, 0, sizeof(httpd_uri_t));
    js_uri.uri = "/app.js";
    js_uri.method = HTTP_GET;
    js_uri.handler = jsHandler;
    httpd_register_uri_handler(_server_handle, &js_uri);

    httpd_uri_t ws_uri;
    memset(&ws_uri, 0, sizeof(httpd_uri_t));
    ws_uri.uri = "/ws";
    ws_uri.method = HTTP_GET;
    ws_uri.handler = wsHandler;
    ws_uri.user_ctx = nullptr;
    ws_uri.is_websocket = true;
    
    httpd_register_uri_handler(_server_handle, &ws_uri);
}

// Обработчик данных полученых по WebSocket(связь между кнопками в браузере и логикой на ЕСП32)
esp_err_t WebServer::wsHandler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "New WebSocket client handshake successful.");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = nullptr;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0) {
        // Защитный лимит на размер пакета, чтобы исключить переполнение памяти
        if (ws_pkt.len > 512) {
            ESP_LOGE(TAG, "Rejected: WebSocket packet too large (%d bytes)", ws_pkt.len);
            return ESP_FAIL;
        }

        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);  //Ловит сырые радиоволны Wi-Fi и собирает из них текстовую строчку, которую прислал браузер
        
        if (ret != ESP_OK) { free(buf); return ret; }

        cJSON *json = cJSON_Parse((char*)ws_pkt.payload);   // Берёт эту строчку и превращает её в структурированное дерево (JSON-объект)
        if (json != nullptr) {

            cJSON *command = cJSON_GetObjectItem(json, "command");

            if (cJSON_IsString(command)) {

                std::string cmd_str = command->valuestring;
                // Сценарий 1: Ловим массив точек программы перемещений (нажата кнопка "start_program")
                if (cmd_str == "start_program") {

                    cJSON *points_array = cJSON_GetObjectItem(json, "points");
                    if (points_array && cJSON_IsArray(points_array)) {

                        std::vector<int32_t> program_points;
                        int array_size = cJSON_GetArraySize(points_array);
                        
                        //  пересыпаем цифры полученные из браузера с JSON в массив Си++
                        for (int i = 0; i < array_size; i++) {
                            cJSON *point = cJSON_GetArrayItem(points_array, i);
                            if (cJSON_IsNumber(point)) {
                                program_points.push_back(point->valueint);
                            }
                        }
                        // Отдаем массив точек Координатору автоматики во FreeRTOS 
                        DeviceManager::getInstance().startProgramSequence(program_points);
                    }
                }
                // Сценарий 2: Ловим команду принудительной остановки всей программы 
                else if (cmd_str == "stop_program") {
                    DeviceManager::getInstance().stopProgramSequence();
                    DeviceManager::getInstance().executeCommand("stepper", "stop", 0); // Аппаратно тормозим МК 
                }
                // Сценарий 3: Ручное классическое пошаговое управление (GOTO / HOME / STOP) 
                else {
                    cJSON *device = cJSON_GetObjectItem(json, "device");
                    cJSON *pos = cJSON_GetObjectItem(json, "position");

                    if (cJSON_IsString(device)) {
                        int32_t val = cJSON_IsNumber(pos) ? pos->valueint : 0;
                        DeviceManager::getInstance().executeCommand(device->valuestring, cmd_str, val);
                    }
                }
            }
            cJSON_Delete(json);
        }
        free(buf);
    }
    return ESP_OK;
}

// Рассылает телеметрию в браузер пользователя
void WebServer::broadcastWsMessage(const std::string& message) {
    if (_server_handle == nullptr) return;

    size_t max_clients = 7;
    int client_fds[7] = {0}; 

    if (httpd_get_client_list(_server_handle, &max_clients, client_fds) == ESP_OK) {
        for (size_t i = 0; i < max_clients; i++) {
            if (httpd_ws_get_fd_info(_server_handle, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                
                httpd_ws_frame_t packet;
                memset(&packet, 0, sizeof(httpd_ws_frame_t));
                packet.final = true;
                packet.fragmented = false;
                packet.type = HTTPD_WS_TYPE_TEXT;
                packet.payload = (uint8_t*)message.c_str();
                packet.len = message.length();

                httpd_ws_send_frame_async(_server_handle, client_fds[i], &packet);
            }
        }
    }
}

// Чтение файлов .html, .css, .js с флеш-памяти и отправка их чанками
esp_err_t WebServer::sendFile(httpd_req_t *req, const char* filepath, const char* mime_type) {
    FILE* file = fopen(filepath, "r");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, mime_type);

    char* buffer = (char*)malloc(FILE_BUFFER_SIZE);
    if (buffer == nullptr) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes;
    do {
        read_bytes = fread(buffer, 1, FILE_BUFFER_SIZE, file);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
                free(buffer);
                fclose(file);
                ESP_LOGE(TAG, "Abort: File transmission failed.");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);

    free(buffer);
    fclose(file);
    
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t WebServer::indexHandler(httpd_req_t *req) {
    return sendFile(req, "/storage/index.html", "text/html");
}

esp_err_t WebServer::cssHandler(httpd_req_t *req) {
    return sendFile(req, "/storage/style.css", "text/css");
}

esp_err_t WebServer::jsHandler(httpd_req_t *req) {
    return sendFile(req, "/storage/app.js", "application/javascript");
}