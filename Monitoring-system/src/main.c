
#include <esp_log.h>
#include <esp_system.h>

#include <mqtt_client.h>
#include <esp_http_server.h>
#include "esp_timer.h"

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



// https://github.com/espressif/esp32-camera
#define CAM_PIN_PWDN -1  //  the camera can be turned off 
#define CAM_PIN_RESET -1 // reset will be performed
#define CAM_PIN_XCLK 21  //external clock line
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

#define PART_BOUNDARY "123456789000000000000987654321"  // boundary marker between parts in the multipart response
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %lu\r\n\r\n";


QueueHandle_t soundToMQTTQueue;  // Queue to send values from Sound sensor task to publish message task
QueueHandle_t motionToMQTTQueue; //  Queue to send from values Sound sensor task to publish message task

//Array to save the calibrated values of sensors 
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
    //sccb used for communication between the microcontroller and the camera (similar to I2c)
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    //camera's data lines
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,


    .pin_vsync = CAM_PIN_VSYNC, //helps the microcontroller synchronize frame capture
    .pin_href = CAM_PIN_HREF, // Href tells data lines when is valid and should be read 
    .pin_pclk = CAM_PIN_PCLK,  // allows to capture each pixel of the image

    .xclk_freq_hz = 20000000, // The frequency at which the external clock generator (XCLK) of the camera module is operated,
                              // A higher XCLK frequency normally enables faster image acquisition, as the pixels can be read out more quickly.
                              // for our Example its recommended not to go above 20MHz
    
    // used to generate a high-precision clock signal
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // JPEG
    .frame_size = FRAMESIZE_VGA,    //  640x480  (Do not use sizes above QVGA when not JPEG )

    .jpeg_quality = 10, // 0-63 lower number means higher quality (4 and above recommended)
    .fb_count = 1      // number of frame buffers allocated for camera initialization
};                

// HTTP GET handler designed to capture images from the ESP32 camera
static esp_err_t capture_and_send_image(httpd_req_t *req)
{
    camera_fb_t * fb = NULL;  // a pointer for the frame buffer
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len; //length of the JPEG buffer
    uint8_t * _jpg_buf;  // store the address of the JPEG image buffer
    char * part_buf[64];  // to store the multipart message header

    // to record the time of an event
    static int64_t last_frame = 0;  
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE); //tells the client to expect multiple parts in the response
    if(res != ESP_OK){
        return res;
    }

    while(true){
        fb = esp_camera_fb_get();  //  hold the image data 
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        // if not JPG format then transform frame into JPG
        if(fb->format != PIXFORMAT_JPEG){
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len); //80 is the quality
            if(!jpeg_converted){
                ESP_LOGE(TAG, "JPEG compression failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        // sending HTTP response data in smaller parts or chunks
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(fb->format != PIXFORMAT_JPEG){
            free(_jpg_buf);
        }
        esp_camera_fb_return(fb);  // return frame buffer to be reused again
        if(res != ESP_OK){
            break;
        }
        
        //printing out how long the even took, framerate and size 
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        ESP_LOGI(TAG, "MJPG: %luKB %lums (%.1ffps)",
            (uint32_t)(_jpg_buf_len/1024),
            (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    last_frame = 0;
    return res;
}

//URI handler for the /capture path
httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_and_send_image,
    .user_ctx = NULL};

// starts the web server and registers URI handlers
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

// a function to store the values in the right Sensor-Array
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


// function to receive sound Values 
void sound_sensor(void *pvParameters)
{

    int adc_raw;  // Stores the raw ADC value
    int voltage; //Stores the calibrated values

    while (true)
    {
        int c = 0;
        while (c < 10)  // before sending reads 10 time from sound sensor
        {
            //https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/adc_oneshot.html
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_4, &adc_raw));  // gets the raw voltage values from sound sensor
            ESP_LOGI("sound_sensor", "sound_sensor_one_shot raw_values %dmV \n",adc_raw );

            //https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/adc_calibration.html
            bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_4, ADC_ATTEN_DB_0, &adc1_cali_chan4_handle); // initialize the calibration

            ESP_LOGI("sound_sensor", "sound_sensor_one_shot raw value: %d", adc_raw);
            if (do_calibration1_chan0)
            {
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan4_handle, adc_raw, &voltage)); // converts the raw ADC value to a calibrated voltage
                ESP_LOGI("sound_sensor", "sound_sensor_one_shot voltage %dmV \n", voltage);

                arraytracker(voltage, c, 1); // stores it in the Sound sensor Array
                c++;
            }

            vTaskDelay(pdMS_TO_TICKS(500));
        }

        /*
        for (int i = 0; i < avg_sound_SIZE; i++)
        {
            printf("%d \n", average_data.avg_sound[i]);
        }*/

        char printarr[avg_sound_SIZE * 2 - 1];
        char *result = printArray(average_data.avg_sound, avg_sound_SIZE, printarr);  

        printf("sound_array : %s \n", result);

        int averag_sound_calculated = avgCalcu(average_data.avg_sound, avg_sound_SIZE); // calculate the average value of Array

        printf("result : %d \n", averag_sound_calculated);

        if (xQueueSend(soundToMQTTQueue, &averag_sound_calculated, ((TickType_t)5)) == pdTRUE) // sends the average Value to the publish message task
        {
            ESP_LOGI("soundToMQTTQueue", "Sound Sent ");
        }
    }
}

// function to receive motion Values 
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

        /*
        for (int i = 0; i < avg_motion_SIZE; i++)
        {
            printf("%d \n", average_data.avg_motion[i]);
        }*/

        char printarr[avg_motion_SIZE * 2 - 1];
        char *result = printArray(average_data.avg_motion, avg_motion_SIZE, printarr);

        printf("motion_array: %s\n", result);

        int averag_motion_calculated = avgCalcu(average_data.avg_motion, avg_motion_SIZE);

        printf("result : %d \n", averag_motion_calculated);

        if (xQueueSend(motionToMQTTQueue, &averag_motion_calculated, ((TickType_t)5)) == pdTRUE)  // sends the average Value to the publish message task
        {
            ESP_LOGI("motionToMQTTQueue", "motion Sent ");
        }
    }
}


 //publish sensor data MQTT topics
void publish_message()
{
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/mqtt.html
   esp_mqtt_client_publish(client, "monitoring-system/ip-address", ipAddress, 0, 0, 1); // publishes the IP-Adress of the Website to a MQTT topic

    while (1)
    {
        int sound;
        int motion;

        //receives the average data from the Sound and motion task
        if (xQueueReceive(soundToMQTTQueue, &sound, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "Sound received: %d \n", sound);
        }

        if (xQueueReceive(motionToMQTTQueue, &motion, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "motion received: %d \n", motion);
        }

        //converts the received Values into char arrays
        char buffer1[10];
        char buffer2[10];

        int8_t ret1 = snprintf(buffer1, sizeof buffer1, "%.2d", sound);
        int8_t ret2 = snprintf(buffer2, sizeof buffer2, "%.2d", motion);

        if (ret1 < 0 || ret2 < 0)
        {
            ESP_LOGE("convert to char", "failed to convert Int to char");
        }

        // publish both Strings to the MQTT topic
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

    client = mqttclient(); // create MQTT client


    // the Queues so the tasks can communicate with eachothers
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

    // the tasks
    xTaskCreate(publish_message, "publish message", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL); 

    xTaskCreate(sound_sensor, "Sound Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    xTaskCreate(motion_sensor, "motion Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    // Stop the HTTP webserver
    // stop_webserver(server);
}
