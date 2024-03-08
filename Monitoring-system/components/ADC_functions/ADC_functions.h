
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include <esp_system.h>
#include <esp_log.h>

#define ADC_ATTEN_0db 0
#define ADC_WIDTH_12Bit 3
#define DEFAULT_VREF 1100


extern char *TAG;

extern adc_oneshot_unit_handle_t adc1_handle;
extern adc_cali_handle_t adc1_cali_chan4_handle ;
extern adc_cali_handle_t adc1_cali_chan5_handle ;

bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);

void initializeADC_OneShot();
