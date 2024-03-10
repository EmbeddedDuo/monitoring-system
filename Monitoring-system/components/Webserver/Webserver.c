#include <esp_system.h>
#include <esp_http_server.h>
#include "Webserver.h"


const char *CameraTAG = "example:take_picture";

const char *index_html =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    " <script> \n"
    "    function fetchImage() { \n"
    "        fetch(\"/capture\") \n"
    "            .then(response => response.blob()) \n"
    "            .then(blob => { \n"
    "                const imageUrl = URL.createObjectURL(blob); \n"
    "                document.getElementById(\"capturedImage\").src = imageUrl; \n"
    "            }) \n"
    "            .catch(error => console.error('Error fetching image:', error)); \n"
    "    } \n"
    "</script> \n"
    "<head>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    "    <title>ESP32 Camera Image</title>\n"
    "     <style> \n"
    "       #capturedImage { \n"
    "       width: 100%; \n"
    "       height: auto; \n"
    "       } \n"
    "     </style> \n"
    "</head>\n"
    "<body>\n"
    "    <img  id=\"capturedImage\" src=\"/capture\" alt=\"Captured Image\" >\n"
    "</body>\n"
    "</html>\n";



esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, strlen(index_html));
    return ESP_OK;
}

httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL};


void stop_webserver(httpd_handle_t server)
{
    httpd_stop(server);
}