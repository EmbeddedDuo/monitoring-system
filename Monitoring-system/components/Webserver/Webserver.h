#include <esp_system.h>
#include <esp_http_server.h>

/**
 * @brief Web-Interface, das zur Anzeige eines von der ESP32-Kamera aufgenommenen Bildes bietet
 * 
 */
extern const char *index_html;

extern const char *CameraTAG ;

extern httpd_uri_t index_uri ;


/**
 * @brief 
 * 
 * @param req 
 * @return esp_err_t 
 */
esp_err_t index_handler(httpd_req_t *req);

/**
 * @brief 
 * 
 * @param server 
 */
void stop_webserver(httpd_handle_t server);

