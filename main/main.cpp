extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#include <driver/temp_sensor.h>
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

static ssr_ctrl_t s_ssr1;
static ssr_ctrl_t s_ssr2;
static bool _go = false;

static void _read_onboard_temp() {
    temp_sensor_config_t temp_sensor = {
            .dac_offset = TSENS_DAC_L2,
            .clk_div = 6,
    };
    temp_sensor_set_config(temp_sensor);
    temp_sensor_start();
    static int num_readings = 50;
    double temp = 0;
    for (int i = 0; i < num_readings; i++) {
        vTaskDelay(10 / portTICK_RATE_MS);
        float tsens_out;
        temp_sensor_read_celsius(&tsens_out);
        temp += tsens_out;
    }
    ESP_LOGI(TAG, "Internal temp: %fC", temp / num_readings);
    temp_sensor_stop();
}

static void _start_control_loop() {
    // Turn off heat
    ssr_ctlr_set_duty(s_ssr1, 0);
    ssr_ctlr_set_duty(s_ssr2, 0);

    while (_go) {
        double balance = balance_read_percent();
        double input_duty = power_duty_get();
        double output_duty = input_duty * balance / 100 * MAX_SECONDARY_HEAT_RATIO;
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

    _read_onboard_temp();
    level_shifter_init();
    level_shifter_enable(true);
    panel_inputs_init();
    max31850_init();
    balancer_init();

    // Init SSRs
    s_ssr1 = {.gpio = GPIO_NUM_10, RMT_CHANNEL_0, MAINS_50HZ, 0};
    ssr_ctlr_init(s_ssr1);
    s_ssr2 = {.gpio = GPIO_NUM_11, RMT_CHANNEL_1, MAINS_50HZ, 0};
    ssr_ctlr_init(s_ssr2);

    // Init reading inbound power duty
    power_duty_init(GPIO_NUM_6);

    _go = true;
    _start_control_loop();

}