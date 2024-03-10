#include <esp_system.h>
#include <esp_http_server.h>

/**
 * @brief Web interface that allows you to view an image captured by the ESP32 camera
 * 
 */
extern const char *index_html;  

/**
 * @brief TAG for Debugging purpose 
 * 
 */
extern const char *CameraTAG ;

/**
 * @brief  URI handler for the (/) Path
 * 
 */

extern httpd_uri_t index_uri ;


/**
 * @brief handles HTTP GET requests to the root URL (/)
 * 
 * @param req 
 * @return esp_err_t 
 */
esp_err_t index_handler(httpd_req_t *req);

/**
 * @brief stops the webserver
 * 
 * @param server 
 */
void stop_webserver(httpd_handle_t server);

