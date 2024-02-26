#ifndef __WIFI_H__
#define __WIFI_H__

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "stdio.h"
#include "esp_netif.h"

extern bool wifi_established;

/**
 * @brief function for initialising wifi
 * 
 */
void init_wifi();

#endif