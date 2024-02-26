#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <stdlib.h>
#include <mqtt_client.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <esp_log.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/i2c.h"
#include "math.h"
#include <string.h>
#include <esp_log.h>
#include "esp_adc/adc_cali.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_adc/adc_cali_scheme.h"
#include <inttypes.h>

#include <freertos/queue.h>

#include "wifi.h"

esp_mqtt_client_handle_t client;

static const char *mqttTag = "mqtt";

#define ADC_ATTEN_0db 0
#define ADC_WIDTH_12Bit 3
static const adc_unit_t unit = ADC_UNIT_1;
#define DEFAULT_VREF 1100

QueueHandle_t soundToMQTTQueue;
QueueHandle_t motionToMQTTQueue;

void sound_sensor(void *pvParameters)
{
    uint32_t reading;
    uint32_t voltage;

    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11));

    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, adc_chars);
    // Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    {
        printf("eFuse Vref\n");
    }
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    {
        printf("Two Point\n");
    }
    else
    {
        printf("Default\n");
    }

    while (true)
    {

        reading = adc1_get_raw(ADC1_CHANNEL_4);
        voltage = esp_adc_cal_raw_to_voltage(reading, adc_chars);
        ESP_LOGI("Sound Sensor", "%lu mV", voltage);

        if (xQueueSend(soundToMQTTQueue, &voltage, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "Sound Sent ");
        }

         vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void motion_sensor(void *pvParameters)
{
    uint32_t reading;
    uint32_t voltage;

    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11));

    esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, adc_chars);
    // Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    {
        printf("eFuse Vref\n");
    }
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    {
        printf("Two Point\n");
    }
    else
    {
        printf("Default\n");
    }

    while (true)
    {

        reading = adc1_get_raw(ADC1_CHANNEL_5);
        voltage = esp_adc_cal_raw_to_voltage(reading, adc_chars);
        ESP_LOGI("Motion Sensor", "%lu mV", voltage);

        
        if (xQueueSend(motionToMQTTQueue, &voltage, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("motionToMQTTQueue", "motion Sent ");
        } 

         vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(mqttTag, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
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
        .broker.address.uri = "mqtt://193.174.24.220:1883"};

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    return client;
}
void publish_message()
{
    while (1)
    {
        uint32_t sound;
        uint32_t motion;

        if (xQueueReceive(soundToMQTTQueue, &sound, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "Sound received: %lu \n", sound);
        }

        if (xQueueReceive(motionToMQTTQueue, &motion, ((TickType_t)5)) == pdTRUE)
        {
            ESP_LOGI("soundToMQTTQueue", "motion received: %lu \n", motion);
        }

        char buffer1[10];
        char buffer2[10];

        int8_t ret1 = snprintf(buffer1, sizeof buffer1, "%.2lu", sound);
        int8_t ret2 = snprintf(buffer2, sizeof buffer2, "%.2lu", motion);

        if (ret1 < 0 || ret2 < 0)
        {
            ESP_LOGE("convert float to char", "failed to convert float to char");
        }

        esp_mqtt_client_publish(client, "monitoring-system/sound_sensor", buffer1, 0, 0, 0);
        esp_mqtt_client_publish(client, "monitoring-system/motion_sensor", buffer2, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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


    xTaskCreate(publish_message, "publish message", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    xTaskCreate(sound_sensor, "Sound Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    xTaskCreate(motion_sensor, "motion Sensor", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);
}