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
#include "input_power_duty.h"
#include "digital_input.h"

#define TAG  "main"

#define MAX_SECONDARY_HEAT_RATIO 0.6f;
#define ONEWIRE_PIN             GPIO_NUM_2
#define DRUM_MOTOR_SIGNAL_PIN   GPIO_NUM_7

#define TEMPERATURE_TC_MAX      270.0f
#define TEMPERATURE_BOARD_MAX   75.0f


static ssr_ctrl_t s_ssr1;
static ssr_ctrl_t s_ssr2;
static bool _go = false;
static uint64_t s_max31850_addr = 0;

/*
 * Checks that the TC and environmental temperatures are within acceptable range
 */
static bool _temperature_ok() {
    bool is_ok = false;

    // Get TC value
    max31850_data_t elm_temp = max31850_read(ONEWIRE_PIN, s_max31850_addr);
    if (elm_temp.is_valid) {
        if (elm_temp.thermocouple_status == MAX31850_TC_STATUS_OK) {
            ESP_LOGI(TAG, "Thermocouple=%fC, Board=%fC", elm_temp.thermocouple_temp, elm_temp.junction_temp);

            // We want to make sure we are under max temperatures allowed
            if (elm_temp.thermocouple_temp <= TEMPERATURE_TC_MAX && elm_temp.junction_temp < TEMPERATURE_BOARD_MAX) {
                is_ok = true;
            } else {
                ESP_LOGW(TAG, "Max temperature exceed Max TC=%f, Max Board=%f", TEMPERATURE_TC_MAX, TEMPERATURE_BOARD_MAX);
            }
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

    return is_ok;
}

/*
 * Ensures conditions are met so we can start controlling
 *
 * The temperatures read must be within allowed ranges.
 * The main drum must be spinning.
 */
static bool _can_control() {
    bool is_ok =  _temperature_ok();

    if(digital_input_get_level(DRUM_MOTOR_SIGNAL_PIN) == 0) {
        is_ok = false;
        ESP_LOGW(TAG, "Drum motor OFF, not applying heat");
    }

    return is_ok;
}


static void _start_control_loop() {
    // Turn off heat
    level_shifter_enable(true);
    ssr_ctlr_set_duty(s_ssr1, 0);
    ssr_ctlr_set_duty(s_ssr2, 0);

    while (_go) {
        double input_duty = input_power_duty_get();
        double output_duty = 0;
        double balance = balance_read_percent();

        if (_can_control()) {
            output_duty = input_duty * balance / 100 * MAX_SECONDARY_HEAT_RATIO;
        }

        ESP_LOGI(TAG, "Input=%f, Balance: %f, Output=%f", input_duty, balance, output_duty);
        ssr_ctlr_set_duty(s_ssr1, input_duty);
        ssr_ctlr_set_duty(s_ssr2, output_duty);

        vTaskDelay(pdMS_TO_TICKS(1750));
    }

    level_shifter_enable(false);
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");
    gpio_install_isr_service(0);
    sync_signal_generator_init(GPIO_NUM_34, RMT_CHANNEL_3);

    level_shifter_init();
    panel_inputs_init();
    balancer_init();
    digital_input_init(DRUM_MOTOR_SIGNAL_PIN);

    // Thermocouple amplifier
    max3185_devices_t found_devices = max31850_list(ONEWIRE_PIN);
    ESP_ERROR_CHECK(found_devices.devices_address_length == 1 ? ESP_OK : ESP_FAIL);
    s_max31850_addr = found_devices.devices_address[0];

    // Init SSRs
    s_ssr1 = {.gpio = GPIO_NUM_10, .channel = RMT_CHANNEL_0, .mains_hz= MAINS_50HZ, .duty = 0};
    ssr_ctlr_init(s_ssr1);
    s_ssr2 = {.gpio = GPIO_NUM_11, .channel = RMT_CHANNEL_1, .mains_hz= MAINS_50HZ, .duty = 0};
    ssr_ctlr_init(s_ssr2);

    // Init reading inbound power duty
    input_power_duty_init(GPIO_NUM_6);

    _go = true;
    _start_control_loop();

}