#include "digital_input.h"

#include <driver/gpio.h>

bool digital_input_is_on(gpio_num_t gpio) {
    return gpio_get_level(gpio) == 0;
}

void digital_input_init(gpio_num_t gpio) {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << gpio)
    );

    // Pull-up high is disable by default, safety state
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
}
