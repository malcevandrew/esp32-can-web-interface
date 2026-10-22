#include "WebServer.h"       
#include "esp_http_server.h" 
#include "DeviceManager.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h" 
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <vector>         

static const char *TAG = "web_server";      
#define FILE_BUFFER_SIZE 1024

// Обращение к единственному экземпляру класса WebServer
WebServer& WebServer::getInstance() {
    static DeviceManager& manager = DeviceManager::getInstance(); 
    static WebServer instance;
    return instance;
}

// Инициализация файловой системы SPIFFS для хранения файлов отображения веб-интерфейса в директории /storage (app.js, style.css и index.html)
void WebServer::initFS() {
    ESP_LOGI(TAG, "Mounting VFS Flash partition...");

    // Заполняем конфигурационную структуру
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",      
        .partition_label = "storage", 
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf); // Монтируем файловую систему по конфигурационной структуре
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount partition. Check filesystem image.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find partition label 'storage'.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Partition mounted successfully at /storage");
}

// Запуск HTTP-сервера
void WebServer::start() {
    initFS();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();      // По умолчанию порт 80
    config.lru_purge_enable = true; 
    config.uri_match_fn = httpd_uri_match_wildcard; 

    ESP_LOGI(TAG, "Starting HTTP Server on port: %d", config.server_port);
    
    if (httpd_start(&_server_handle, &config) == ESP_OK) {
        registerRoutes();
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server!");
    }
}

// Связываем текстовый адрес страницы (URI) и функцию-обработчик, вызываемую при обращении браузера к этому адресу
void WebServer::registerRoutes() {
    httpd_uri_t index_uri;
    memset(&index_uri, 0, sizeof(httpd_uri_t));
    index_uri.uri = "/";                                            // Адрес
    index_uri.method = HTTP_GET;                                    // Тип запроса - HTTP_GET (просмотр)
    index_uri.handler = indexHandler;                               // Функция-обработчик
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

    httpd_ws_frame_t ws_pkt;                        //  Создаем пустую структуру сетевого кадра ws_pkt
    uint8_t *buf = nullptr;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;               // Указываем серверу, что ожидаем от браузера именно текстовые сообщения

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);   // «Холостая» попытка принять кадр,
    if (ret != ESP_OK) return ret;                          // чтобы узнать длину прилетевшей строчки текста в байтах

    if (ws_pkt.len > 0) {
        // Защитный лимит на размер пакета, чтобы исключить переполнение памяти
        if (ws_pkt.len > 512) {
            ESP_LOGE(TAG, "Rejected: WebSocket packet too large (%d bytes)", ws_pkt.len);
            return ESP_FAIL;
        }

        buf = (uint8_t*)calloc(1, ws_pkt.len + 1);    // Динамически выделяем память размером в длину строки плюс один байт для символа окончания строки \0
        ws_pkt.payload = buf;                         // Привязываем этот буфер к телу кадра (payload)
        
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);  // Собираем текстовую строчку, которую прислал браузер
        
        if (ret != ESP_OK) { free(buf); return ret; }        // Освобождаем выделенную динамически память, если сбой

        cJSON *json = cJSON_Parse((char*)ws_pkt.payload);   // Передаем в парсер библиотеки cJSON. Он переводит её в структурированное дерево (JSON-объект)
        if (json != nullptr) {

            cJSON *command = cJSON_GetObjectItem(json, "command");  // Достаем из JSON-пакета значение команды

            if (cJSON_IsString(command)) {

                std::string cmd_str = command->valuestring;

                // Сценарий 1: Ловим массив точек программы перемещений (нажата кнопка "start_program")
                if (cmd_str == "start_program") {

                    cJSON *points_array = cJSON_GetObjectItem(json, "points");      // Достаем из JSON-пакета набор координат
                    if (points_array && cJSON_IsArray(points_array)) {

                        std::vector<int32_t> program_points;
                        int array_size = cJSON_GetArraySize(points_array);
                        
                        //  Переносим цифры полученные в браузера из JSON в вектор С++
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
            cJSON_Delete(json);     // Удаляем объект
        }
        free(buf);  // Очищаем память во избежании утечки
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

// Чтение файлов .html, .css, .js с флеш-памяти и отправка их частями во избежании нехватки памяти на передачу целиком
esp_err_t WebServer::sendFile(httpd_req_t *req, const char* filepath, const char* mime_type) {
    
    // В режиме чтения проверяем существует ли такой файл
    FILE* file = fopen(filepath, "r");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, mime_type);    // Сообщаем тип данных к передаче

    char* buffer = (char*)malloc(FILE_BUFFER_SIZE);     // Выделяем временный буфер
    if (buffer == nullptr) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes;  // Байт прочитано
    do { 
         // Сохраняем из флеш-памяти в buffer кусочек файла (размером максимум в 1024 байта)
        read_bytes = fread(buffer, 1, FILE_BUFFER_SIZE, file); 

        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) { // отправляем буфер 
                free(buffer);
                fclose(file);
                ESP_LOGE(TAG, "Abort: File transmission failed.");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);


    // Уничтожаем buffer и закрываем файл
    free(buffer);
    fclose(file);
    
    httpd_resp_send_chunk(req, nullptr, 0); // Сообщаем, что файл передан целиком
    return ESP_OK;
}

// Обработчик GET запроса при подключении к IP-адресу ESP32. Отдает файл index.html из SPIFFS в браузер клиента
esp_err_t WebServer::indexHandler(httpd_req_t *req) {
    return sendFile(req, "/storage/index.html", "text/html");
}

esp_err_t WebServer::cssHandler(httpd_req_t *req) {
    return sendFile(req, "/storage/style.css", "text/css");
}

esp_err_t WebServer::jsHandler(httpd_req_t *req) {
    return sendFile(req, "/storage/app.js", "application/javascript");
}