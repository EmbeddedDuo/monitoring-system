
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include <esp_system.h>
#include <esp_log.h>

#define ADC_ATTEN_0db 0
#define ADC_WIDTH_12Bit 3
#define DEFAULT_VREF 1100

// initialization and configuration of ADC (Analog to Digital Converter), one-shot readings 


/**
 * @brief TAG for debugging purpose 
 * 
 */
extern char *TAG;

/**
 * @brief Pointer to ADC unit
 * 
 */

extern adc_oneshot_unit_handle_t adc1_handle;

/**
 * @brief calibration-handler for GPIO 32
 * 
 */
extern adc_cali_handle_t adc1_cali_chan4_handle ;

/**
 * @brief calibration-handler for GPIO 33
 * 
 */
extern adc_cali_handle_t adc1_cali_chan5_handle ;

/**
 * @brief initialize the calibration for an Analog-to-Digital Converter (ADC) 
 * 
 * @param unit 
 * @param channel 
 * @param atten 
 * @param out_handle  pointer to a handle that will be used to refer to the calibration profile
 * @return true 
 * @return false 
 */
bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);


/**
 * @brief  configures the ADC unit for one-shot readings, which means the ADC performs a single conversion each time it's triggered
 * 
 */
void initializeADC_OneShot();
