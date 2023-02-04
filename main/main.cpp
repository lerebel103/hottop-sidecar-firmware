extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#include <driver/temp_sensor.h>
#include "level_shifter.h"
#include "panel_inputs.h"
#include "max31850.h"
#include "ssr_ctlr.h"
#include "rmt_duty_map.h"
#include "balancer.h"

#define TAG  "main"

void _read_internal_temp() {
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


extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");

    _read_internal_temp();

    level_shifter_init();
    panel_inputs_init();
    level_shifter_enable(true);
    max31850_init();
    balancer_init();

    double balance;
    balance_read(&balance);
    ESP_LOGI(TAG, "Balance: %fC", balance);

    ssr_ctrl_t ssr1 = {.gpio = GPIO_NUM_10, RMT_CHANNEL_0, MAINS_50HZ, 0};
    ssr_ctlr_init(ssr1);
    ssr_ctlr_set_duty(ssr1, 72);

    ssr_ctrl_t ssr2 = {.gpio = GPIO_NUM_11, RMT_CHANNEL_1, MAINS_50HZ, 0};
    ssr_ctlr_init(ssr2);
    ssr_ctlr_set_duty(ssr2, 89);
}