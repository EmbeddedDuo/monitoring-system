#ifndef __WIFI_H__
#define __WIFI_H__

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "stdio.h"
#include "esp_netif.h"

/**
 * @brief to check if the wifi is established
 * 
 */
extern bool wifi_established;

/**
 * @brief to store the IP-Adress for the Website
 * 
 */
extern char ipAddress[16];

/**
 * @brief function for initialising wifi
 * 
 */
void init_wifi();

#endif