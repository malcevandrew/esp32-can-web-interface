#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"
#include <string>

class WebServer {
public:

    // Запрет на клонирование. Удаляем конструктор копирования и оператор присваивания
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    // Обращение к единственному экземпляру класса WebServer
    static WebServer& getInstance();

    // Запуск HTTP-сервера
    void start();
    
    // Метод для отправки данных всем подключенным WebSockets браузерам
    void broadcastWsMessage(const std::string& message);

private:
    WebServer() = default;
    ~WebServer() = default;

    void initFS();
    void registerRoutes();

    static esp_err_t indexHandler(httpd_req_t *req);
    static esp_err_t cssHandler(httpd_req_t *req);
    static esp_err_t jsHandler(httpd_req_t *req);
    
    // Коллбек для обработки входящих фреймов WebSocket
    static esp_err_t wsHandler(httpd_req_t *req);

    static esp_err_t sendFile(httpd_req_t *req, const char* filepath, const char* mime_type);

    httpd_handle_t _server_handle = nullptr;
};

#endif // WEB_SERVER_H
