#include <hal/gpio_types.h>
#include <driver/gpio.h>
#include "panel_inputs.h"


void panel_inputs_init() {
    // --- Configure input switch that drives the pump
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_NUM_6) |
            (1ULL << GPIO_NUM_7) |
            (1ULL << GPIO_NUM_8) |
            (1ULL << GPIO_NUM_9)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}