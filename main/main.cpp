#include <esp_log.h>
#include <driver/gpio.h>
#include "control_loop.h"
#include "square_wave_gen.h"

#define TAG  "main"

static square_wave_handle_t wave_handle;

/**
 * Hototop needs a 100Hz square signal, with 480us low side.
 * We have the option of generating this signal here for testing purposes with a real panel
 */
void _generate_zero_signal() {
    // Here for debugging other boards or self, generates a reference 100Hz signal
    square_wave_cfg_t cfg = {
            .gpio = GPIO_NUM_34,
            .period_us = 10000,
            .low_us = 480
    };
    square_wave_gen_new(cfg, &wave_handle);
    square_wave_gen_start(wave_handle);
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");
    gpio_install_isr_service(0);
    _generate_zero_signal();

    control_loop_init();
    control_loop_run();
}