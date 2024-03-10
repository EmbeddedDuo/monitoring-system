#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <wifi.h>


#define ESP_WIFI_SSID CONFIG_EXAMPLE_WIFI_SSID
#define ESP_WIFI_PASS CONFIG_EXAMPLE_WIFI_PASSWORD

static const char *TAG = "wifi_station";

bool wifi_established = false;

char ipAddress[16]= "";



static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        wifi_established = false;
        ESP_LOGI(TAG, "connect to the AP"); 
        esp_wifi_connect();                          // Establish a connection to the access point (AP)
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_established = false;
        ESP_LOGI(TAG, "retry to connect to the AP"); // Erneuter Verbindungsversuch zum AP
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip : " IPSTR "\n", IP2STR(&event->ip_info.ip)); // Receive IP address
        snprintf(ipAddress, sizeof(ipAddress), "%d.%d.%d.%d", IP2STR(&event->ip_info.ip));
        wifi_established = true;                                            // WLAN connection established
        esp_wifi_set_ps(0);
    }
    else
    {
        ESP_LOGI(TAG, "unhandled event (%s) with ID %lu!", event_base, event_id);  // Untreated event
    }
}

void init_wifi()
{
    esp_netif_init();                                    // Initialize network interface
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Create WLAN initialization configuration
    cfg.nvs_enable = false;  //nvs = Non-Volatile Storage
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                                                                   // Initialize WLAN subsystem
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));   // Register WLAN event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));  // Register IP event handler for successful WLAN connection
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &wifi_event_handler, NULL));  // register IP event handler for lost WLAN connection
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,     // Configure WLAN SSID
            .password = ESP_WIFI_PASS, // Configure WLAN password
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));                   // Set WLAN mode to Station (STA)
    esp_netif_create_default_wifi_sta();                                 // Create a standard WiFi station
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config)); // Set WLAN configuration
    ESP_ERROR_CHECK(esp_wifi_start());                                   // Start WLAN
}