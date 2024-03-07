
#include <esp_log.h>
#include <esp_system.h>

#include <mqtt_client.h>

#include <nvs_flash.h>
#include <string.h>
#include <stdio.h>
#include <esp_http_server.h>

#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <freertos/queue.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include <inttypes.h>


// support IDF 5.x
#ifndef portTICK_RATE_MS
#define portTICK_RATE_MS portTICK_PERIOD_MS
#endif

#include "esp_camera.h"
#include "wifi.h"
#include "self-created-functions.h"

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

esp_mqtt_client_handle_t client;

#define ADC_ATTEN_0db 0
#define ADC_WIDTH_12Bit 3
#define DEFAULT_VREF 1100

static char *TAG = "adc_oneshot";

adc_oneshot_unit_handle_t adc1_handle;
adc_cali_handle_t adc1_cali_chan4_handle = NULL;
adc_cali_handle_t adc1_cali_chan5_handle = NULL;

QueueHandle_t soundToMQTTQueue;
QueueHandle_t motionToMQTTQueue;

struct average_data_t
{
    int avg_sound[10];
    int avg_motion[10];

} average_data;

int avg_sound_SIZE = sizeof(average_data.avg_sound) / sizeof(average_data.avg_sound[0]);
int avg_motion_SIZE = (sizeof(average_data.avg_motion) / sizeof(average_data.avg_motion[0]));

static const char *CameraTAG = "example:take_picture";

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

const char *mqttTag = "mqtt";


void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(mqttTag, "Last error %s: 0x%x", message, error_code);
    }
}

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(mqttTag, "Event dispatched from event loop base =%s , event_id =%lu ", base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(mqttTag, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(mqttTag, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(mqttTag, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(mqttTag, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(mqttTag, "MQTT_EVENT_DATA");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(mqttTag, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(mqttTag, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(mqttTag, "Other event id:%d", event->event_id);
        break;
    }
}

esp_mqtt_client_handle_t mqttclient()
{

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://193.174.29.133:1883"};

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    return client;
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

static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Calibration Success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    }
    else
    {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

void initializeADC_OneShot()
{
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_0,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_4, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_5, &config));
}

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

void stop_webserver(httpd_handle_t server)
{
    httpd_stop(server);
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

            bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_4, ADC_ATTEN_DB_0, &adc1_cali_chan4_handle);

            ESP_LOGI("sound_sensor", "sound_sensor_one_shot raw value: %d", adc_raw);
            if (do_calibration1_chan0)
            {
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan4_handle, adc_raw, &voltage));
                ESP_LOGI("sound_sensor", "sound_sensor_one_shot voltage %dmV \n", voltage);
            }

            vTaskDelay(pdMS_TO_TICKS(500));
            arraytracker(voltage, c, 1);
            c++;
        }

        char printarr[avg_sound_SIZE * 2];
        char *result = printArray(average_data.avg_sound, avg_sound_SIZE, printarr);

        printf("sound_array : %s \n", result);

        int averag_sound_calculated = avgCalcu(average_data.avg_sound, c);

        printf("%d \n", averag_sound_calculated);

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
            }

            vTaskDelay(pdMS_TO_TICKS(500));
            arraytracker(voltage, c, 2);
            c++;
        }

        char printarr[avg_motion_SIZE * 2];
        char *result = printArray(average_data.avg_motion, sizeof(average_data.avg_motion) / sizeof(average_data.avg_motion[0]), printarr);

        printf("motion_array: %s\n", result);

        int averag_motion_calculated = avgCalcu(average_data.avg_motion, c);

        printf("%d \n", averag_motion_calculated);

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
    //httpd_handle_t server = start_webserver();
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

    xTaskCreate(publish_message, "publish message", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);  // send messages to mqtt

    xTaskCreate(sound_sensor, "Sound Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    xTaskCreate(motion_sensor, "motion Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    // Stop the HTTP webserver
    //stop_webserver(server);
}
