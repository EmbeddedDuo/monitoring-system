/**
 * This example takes a picture every 5s and print its size on serial monitor.
 */

// =============================== SETUP ======================================

// 1. Board setup (Uncomment):
// #define BOARD_WROVER_KIT
// #define BOARD_ESP32CAM_AITHINKER

/**
 * 2. Kconfig setup
 *
 * If you have a Kconfig file, copy the content from
 *  https://github.com/espressif/esp32-camera/blob/master/Kconfig into it.
 * In case you haven't, copy and paste this Kconfig file inside the src directory.
 * This Kconfig file has definitions that allows more control over the camera and
 * how it will be initialized.
 */

/**
 * 3. Enable PSRAM on sdkconfig:
 *
 * CONFIG_ESP32_SPIRAM_SUPPORT=y
 *
 * More info on
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/kconfig.html#config-esp32-spiram-support
 */

// ================================ CODE ======================================

#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>
#include <stdio.h>
#include <esp_http_server.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_spiffs.h"

// support IDF 5.x
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

#include "esp_camera.h"
#include "wifi.h"

#define BOARD_WROVER_KIT 1

// WROVER-KIT PIN Map
#ifdef BOARD_WROVER_KIT

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

#endif

// ESP32Cam (AiThinker) PIN Map
#ifdef BOARD_ESP32CAM_AITHINKER

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 // software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#endif

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
    "    setInterval(fetchImage, 2000); \n"
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

    // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 2000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,    // QQVGA-UXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 12, // 0-63 lower number means higher quality
    .fb_count = 5       // if more than one, i2s runs in continuous mode. Use only with JPEG
};

static esp_err_t init_camera(int framesize)
{
    // initialize the camera
    camera_config.frame_size = framesize;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

esp_err_t mountSPIFFS(char *partition_label, char *base_path)
{
    ESP_LOGI(TAG, "Initializing SPIFFS file system");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = partition_label,
        .max_files = 8,
        .format_if_mount_failed = true};

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    ESP_LOGI(TAG, "Mount SPIFFS filesystem");
    return ret;
}

static esp_err_t camera_capture(char *FileName, size_t *pictureSize)
{
    // clear internal queue
    // for(int i=0;i<2;i++) {
    for (int i = 0; i < 1; i++)
    {
        camera_fb_t *fb = esp_camera_fb_get();
        ESP_LOGI(TAG, "fb->len=%d", fb->len);
        esp_camera_fb_return(fb);
    }

    // acquire a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera Capture Failed");
        return ESP_FAIL;
    }

    // replace this with your own function
    // process_image(fb->width, fb->height, fb->format, fb->buf, fb->len);
    FILE *f = fopen(FileName, "wb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fwrite(fb->buf, fb->len, 1, f);
    ESP_LOGI(TAG, "fb->len=%d", fb->len);
    *pictureSize = (size_t)fb->len;
    fclose(f);

    // return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    return ESP_OK;
}

#endif

static esp_err_t capture_and_send_image(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
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
    .user_ctx = NULL};

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &capture_uri);
        httpd_register_uri_handler(server, &index_uri); // Register the URI handler for the index page
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
#if ESP_CAMERA_SUPPORTED
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

    // Initialize SPIFFS
    ESP_LOGI(TAG, "Initializing SPIFFS");
    char *partition_label = "storage";
    char *base_path = "/spiffs";
    ret = mountSPIFFS(partition_label, base_path);
    if (ret != ESP_OK)
        return;

#if CONFIG_ENABLE_FLASH
    // Enable Flash Light
    // gpio_pad_select_gpio(CONFIG_GPIO_FLASH);
    gpio_reset_pin(CONFIG_GPIO_FLASH);
    gpio_set_direction(CONFIG_GPIO_FLASH, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_GPIO_FLASH, 0);
#endif

    int framesize = FRAMESIZE_VGA;

    ret = init_camera(framesize);
    if (ESP_OK != ret)
    {
        return;
    }

    size_t pictureSize;
    char localFileName[64];
    snprintf(localFileName, sizeof(localFileName) - 1, "%s/picture.jpg", base_path);

    sprintf(localFileName, "%s", CONFIG_FIXED_REMOTE_FILE);

    ESP_LOGI(TAG, "remoteFileName=%s", localFileName);

    httpd_handle_t server = start_webserver();

#if CONFIG_ENABLE_FLASH
    // Flash Light ON
    gpio_set_level(CONFIG_GPIO_FLASH, 1);
#endif

    while (1)
    {
        ESP_LOGI(TAG, "Taking picture...");

        ret = camera_capture(localFileName, &pictureSize);
        ESP_LOGI(TAG, "camera_capture=%d", ret);
        ESP_LOGI(TAG, "pictureSize=%d", pictureSize);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    stop_webserver(server);

#else
    ESP_LOGE(TAG, "Camera support is not available for this chip");
    return;
#endif
}
