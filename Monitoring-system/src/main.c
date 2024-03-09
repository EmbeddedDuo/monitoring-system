
#include <esp_log.h>
#include <esp_system.h>

#include <mqtt_client.h>
#include <esp_http_server.h>

#include <nvs_flash.h>
#include <string.h>
#include <stdio.h>

#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <freertos/queue.h>

#include <inttypes.h>

// support IDF 5.x
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

#include "esp_camera.h"
#include "wifi.h"
#include "self-created-functions.h"
#include "Webserver.h"
#include "Mqtt_publisher.h"
#include "ADC_functions.h"

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

QueueHandle_t soundToMQTTQueue;
QueueHandle_t motionToMQTTQueue;

struct average_data_t
{
    int avg_sound[10];
    int avg_motion[10];

} average_data;

int avg_sound_SIZE = sizeof(average_data.avg_sound) / sizeof(average_data.avg_sound[0]);
int avg_motion_SIZE = (sizeof(average_data.avg_motion) / sizeof(average_data.avg_motion[0]));

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

    .xclk_freq_hz = 20000000, // The frequency at which the external clock generator (XCLK) of the camera module is operated,
                              // A higher XCLK frequency normally enables faster image acquisition, as the pixels can be read out more quickly.
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // JPEG
    .frame_size = FRAMESIZE_VGA,    //  640x480  (Do not use sizes above QVGA when not JPEG )

    .jpeg_quality = 4, // 0-63 lower number means higher quality (4 and above recommended)
    .fb_count = 1      // number of frame buffers allocated for camera initialization
};

static esp_err_t capture_and_send_image(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get(); // Obtain pointer to a frame buffer
    if (!fb)
    {
        ESP_LOGE(CameraTAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return ESP_OK;
}

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
        httpd_register_uri_handler(server, &capture_uri); // Register the URI handler for the image
        httpd_register_uri_handler(server, &index_uri);
        return server;
    }

    return NULL;
}

void arraytracker(int val, int index, int sensorTyp)
{

    switch (sensorTyp)
    {
    case 1:
        if (index < avg_sound_SIZE)
        {
            average_data.avg_sound[index] = val;
        }

        break;
    case 2:
        if (index < avg_motion_SIZE)
        {
            average_data.avg_motion[index] = val;
        }

        break;

    default:
        break;
    }
}

static esp_err_t init_camera()
{
    // initialize the camera and allocates framebuffer
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(CameraTAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

void sound_sensor(void *pvParameters)
{

    int adc_raw;
    int voltage;

    while (true)
    {
        int c = 0;
        while (c < 10)
        {
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_4, &adc_raw));
            ESP_LOGI("sound_sensor", "sound_sensor_one_shot raw_values %dmV \n",adc_raw );

            bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_4, ADC_ATTEN_DB_0, &adc1_cali_chan4_handle);

            ESP_LOGI("sound_sensor", "sound_sensor_one_shot raw value: %d", adc_raw);
            if (do_calibration1_chan0)
            {
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan4_handle, adc_raw, &voltage));
                ESP_LOGI("sound_sensor", "sound_sensor_one_shot voltage %dmV \n", voltage);

                arraytracker(voltage, c, 1);
                c++;
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }

        for (int i = 0; i < avg_sound_SIZE; i++)
        {
            printf("%d \n", average_data.avg_sound[i]);
        }

        char printarr[avg_sound_SIZE * 2 - 1];
        char *result = printArray(average_data.avg_sound, avg_sound_SIZE, printarr);

        printf("sound_array : %s \n", result);

        int averag_sound_calculated = avgCalcu(average_data.avg_sound, avg_sound_SIZE);

        printf("result : %d \n", averag_sound_calculated);

        if (xQueueSend(soundToMQTTQueue, &averag_sound_calculated, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "Sound Sent ");
        }
    }
}

void motion_sensor(void *pvParameters)
{
    int adc_raw;
    int voltage;

    while (true)
    {
        int c = 0;
        while (c < 10)
        {
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_5, &adc_raw));

            bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_5, ADC_ATTEN_DB_0, &adc1_cali_chan5_handle);

            ESP_LOGI("motion_sensor", "motion_sensor_one_shot raw value: %d", adc_raw);
            if (do_calibration1_chan0)
            {
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan5_handle, adc_raw, &voltage));
                ESP_LOGI("motion_sensor", "motion_sensor_one_shot voltage %dmV \n", voltage);

                arraytracker(voltage, c, 2);
                c++;
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }

        for (int i = 0; i < avg_motion_SIZE; i++)
        {
            printf("%d \n", average_data.avg_motion[i]);
        }

        char printarr[avg_motion_SIZE * 2 - 1];
        char *result = printArray(average_data.avg_motion, avg_motion_SIZE, printarr);

        printf("motion_array: %s\n", result);

        int averag_motion_calculated = avgCalcu(average_data.avg_motion, avg_motion_SIZE);

        printf("result : %d \n", averag_motion_calculated);

        if (xQueueSend(motionToMQTTQueue, &averag_motion_calculated, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("motionToMQTTQueue", "motion Sent ");
        }
    }
}

void publish_message()
{

    while (1)
    {
        int sound;
        int motion;

        if (xQueueReceive(soundToMQTTQueue, &sound, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "Sound received: %d \n", sound);
        }

        if (xQueueReceive(motionToMQTTQueue, &motion, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "motion received: %d \n", motion);
        }

        char buffer1[10];
        char buffer2[10];

        int8_t ret1 = snprintf(buffer1, sizeof buffer1, "%.2d", sound);
        int8_t ret2 = snprintf(buffer2, sizeof buffer2, "%.2d", motion);

        if (ret1 < 0 || ret2 < 0)
        {
            ESP_LOGE("convert float to char", "failed to convert float to char");
        }

        esp_mqtt_client_publish(client, "monitoring-system/sound_sensor", buffer1, 0, 0, 0);
        esp_mqtt_client_publish(client, "monitoring-system/motion_sensor", buffer2, 0, 0, 0);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main()
{
    initializeADC_OneShot();

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
    // httpd_handle_t server = start_webserver();
    start_webserver();

    client = mqttclient();

    soundToMQTTQueue = xQueueCreate(2, sizeof(float));
    if (soundToMQTTQueue == NULL)
    {
        ESP_LOGE("soundToMQTTQueue ", "Queue couldn't be created");
    }

    motionToMQTTQueue = xQueueCreate(2, sizeof(float));
    if (motionToMQTTQueue == NULL)
    {
        ESP_LOGE("soundToMQTTQueue ", "Queue couldn't be created");
    }

    xTaskCreate(publish_message, "publish message", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL); // send messages to mqtt

    xTaskCreate(sound_sensor, "Sound Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    xTaskCreate(motion_sensor, "motion Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    // Stop the HTTP webserver
    // stop_webserver(server);
}
