
#include <mqtt_client.h>
#include <esp_log.h>

#include "Mqtt_publisher.h"

const char *mqttTag = "mqtt";

esp_mqtt_client_handle_t client;

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
        .broker.address.uri = "mqtt://193.174.29.133:1883"}; // MQTT client configuration with the broker's URI.

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg); // Initializes the MQTT client with the configuration

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL)); // Registers the event handler function to handle
                                                                                                         // MQTT events for this client
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    return client;  //returns handle 
}