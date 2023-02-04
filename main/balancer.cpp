#include <esp_adc_cal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "balancer.h"


#define DEFAULT_VREF                1100
static const adc_unit_t unit = ADC_UNIT_1;
static esp_adc_cal_characteristics_t *adc_chars;

void balance_read(double* value) {
    static int num_readings = 25;

    // Let things stabilise
    vTaskDelay(pdMS_TO_TICKS(5));

    double level_voltage = 0.0;
    for (int i = 0; i < 25; i++) {
        auto raw = adc1_get_raw(ADC1_CHANNEL_0);
        level_voltage += esp_adc_cal_raw_to_voltage(raw, adc_chars);
        ets_delay_us(250);
    }

    *value = level_voltage / num_readings;
}

void balancer_init() {
    // Configure ADC
    auto attenuation = ADC_ATTEN_DB_11;
    adc1_config_width(ADC_WIDTH_BIT_13);
    adc1_config_channel_atten(ADC1_CHANNEL_0, attenuation);

    //Characterize ADC
    adc_chars = static_cast<esp_adc_cal_characteristics_t *>(calloc(1, sizeof(esp_adc_cal_characteristics_t)));
    esp_adc_cal_characterize(unit, attenuation, ADC_WIDTH_BIT_13, DEFAULT_VREF, adc_chars);

}
