
#include <mqtt_client.h>
#include <esp_log.h>

// MQTT client for connecting to an MQTT broker


/**
 * @brief TAG used for debugging purpose 
 * 
 */
extern const char *mqttTag ;

/**
 * @brief Pointer to MQTT Client instance
 * 
 */
extern esp_mqtt_client_handle_t client;

/**
 * @brief method that is used to locate the reason of failure
 * 
 * @param message 
 * @param error_code 
 */
void log_error_if_nonzero(const char *message, int error_code);

/**
 * @brief handle events from the MQTT client
 * 
 * @param handler_args 
 * @param base 
 * @param event_id 
 * @param event_data 
 */
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/**
 * @brief  Initialize and Start the MQTT Client
 * 
 * @return esp_mqtt_client_handle_t 
 */
esp_mqtt_client_handle_t mqttclient();