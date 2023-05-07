#include <esp_log.h>
#include <driver/gpio.h>
#include "control_loop.h"
#include "square_wave_gen.h"

#define TAG  "main"


extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");
    gpio_install_isr_service(0);

    // Here for debugging other boards or self, generates a reference 100Hz signal
    square_wave_cfg_t cfg = {
            .gpio = GPIO_NUM_34,
            .period_us = 10000,
            .low_us = 480
    };
    square_wave_gen_init(cfg);

    control_loop_init();
    control_loop_run();
}