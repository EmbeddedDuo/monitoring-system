
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>
#include <stdio.h>
#include <esp_http_server.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "esp_spiffs.h"

// support IDF 5.x
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

#include "esp_camera.h"
#include "wifi.h"


// WROVER-KIT PIN Map

#define CAM_PIN_PWDN -1  // power down is not used
#define CAM_PIN_RESET -1 // software reset will be performed
#define CAM_PIN_XCLK 21
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 19
#define CAM_PIN_D2 18
#define CAM_PIN_D1 5
#define CAM_PIN_D0 4
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22



static const char *TAG = "example:take_picture";

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
    "    // Call fetchImage every 5 seconds (5000 milliseconds) \n"
    "    setInterval(fetchImage, 100); \n"
    "</script> \n"
    "<head>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    "    <title>ESP32 Camera Image</title>\n"
    "</head>\n"
    "<body>\n"
    "    <h1>ESP32 Camera Image</h1>\n"
    "    <img  id=\"capturedImage\" src=\"/capture\" alt=\"Captured Image\" width=\"640\" height=\"480\">\n"
    "</body>\n"
    "</html>\n";

#if ESP_CAMERA_SUPPORTED
camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000, //The frequency at which the external clock generator (XCLK) of the camera module is operated, 
                                //A higher XCLK frequency normally enables faster image acquisition, as the pixels can be read out more quickly. 
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // JPEG
    .frame_size = FRAMESIZE_VGA,    //  640x480  (Do not use sizes above QVGA when not JPEG )

	.jpeg_quality = 4, //0-63 lower number means higher quality (4 and above recommended)
	.fb_count = 1		// number of frame buffers allocated for camera initialization
};

static esp_err_t init_camera()
{
    // initialize the camera and allocates framebuffer
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

#endif

static esp_err_t capture_and_send_image(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get(); // Obtain pointer to a frame buffer
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

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

httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_and_send_image,
    .user_ctx = NULL };

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &capture_uri); // Register the URI handler for the image
        httpd_register_uri_handler(server, &index_uri); 
        return server;
    }

    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    httpd_stop(server);
}

void app_main()
{
    esp_err_t ret = nvs_flash_init(); // NVS-Flash-Speicher initialisieren
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);                             // Fehlerprüfung
    ESP_ERROR_CHECK(esp_netif_init());                // Netzwerk-Interface initialisieren
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Standard-Event-Schleife erstellen

    init_wifi();

    while (!wifi_established)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }


    // Initialize camera
    ESP_ERROR_CHECK(init_camera());   

    // Start the HTTP webserver
    httpd_handle_t server = start_webserver();

    // Main loop
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Stop the HTTP webserver
    stop_webserver(server);
}

