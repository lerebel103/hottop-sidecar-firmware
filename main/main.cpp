extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#include <driver/gpio.h>
#include "level_shifter.h"
#include "panel_inputs.h"
#include "max31850.h"
#include "ssr_ctlr.h"
#include "rmt_duty_map.h"
#include "balancer.h"
#include "sync_signal_generator.h"
#include "power_duty.h"

#define TAG  "main"

#define MAX_SECONDARY_HEAT_RATIO 0.5f;
#define ONEWIRE_PIN GPIO_NUM_2


static ssr_ctrl_t s_ssr1;
static ssr_ctrl_t s_ssr2;
static bool _go = false;
static uint64_t s_max31850_addr = 0;


static void _start_control_loop() {
    // Turn off heat
    ssr_ctlr_set_duty(s_ssr1, 0);
    ssr_ctlr_set_duty(s_ssr2, 0);

    while (_go) {
        double balance = balance_read_percent();
        double input_duty = 0;
        double output_duty = 0;

        // Get TC value
        max31850_data_t elm_temp = max31850_read(ONEWIRE_PIN, s_max31850_addr);
        if (elm_temp.is_valid) {
            if (elm_temp.thermocouple_status == MAX31850_TC_STATUS_OK) {
                ESP_LOGI(TAG, "Thermocouple=%fC, Board=%fC", elm_temp.thermocouple_temp, elm_temp.junction_temp);

                input_duty = power_duty_get();
                output_duty = input_duty * balance / 100 * MAX_SECONDARY_HEAT_RATIO;

            } else {
                if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_OPEN_CIRCUIT) {
                    ESP_LOGE(TAG, "Unable to run, thermocouple fault OPEN CIRCUIT");
                } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_GND) {
                    ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO GROUND");
                } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_VCC) {
                    ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO VCC");
                }
            }
        } else {
            ESP_LOGE(TAG, "Unable to run, error reading thermocouple data.");
        }


        ESP_LOGI(TAG, "Input=%f, Balance: %f, Output=%f", input_duty, balance, output_duty);
        ssr_ctlr_set_duty(s_ssr1, input_duty);
        ssr_ctlr_set_duty(s_ssr2, output_duty);


        vTaskDelay(pdMS_TO_TICKS(500));
    }

}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");
    gpio_install_isr_service(0);
    sync_signal_generator_init(GPIO_NUM_34, RMT_CHANNEL_3);

    level_shifter_init();
    level_shifter_enable(true);
    panel_inputs_init();
    balancer_init();
    max3185_devices_t found_devices = max31850_list(ONEWIRE_PIN);
    ESP_ERROR_CHECK(found_devices.devices_address_length == 1 ? ESP_OK : ESP_FAIL);
    s_max31850_addr = found_devices.devices_address[0];

    // Init SSRs
    s_ssr1 = {.gpio = GPIO_NUM_10, .channel = RMT_CHANNEL_0, .mains_hz= MAINS_50HZ, .duty = 0};
    ssr_ctlr_init(s_ssr1);
    s_ssr2 = {.gpio = GPIO_NUM_11, .channel = RMT_CHANNEL_1, .mains_hz= MAINS_50HZ, .duty = 0};
    ssr_ctlr_init(s_ssr2);

    // Init reading inbound power duty
    power_duty_init(GPIO_NUM_6);

    _go = true;
    _start_control_loop();

}