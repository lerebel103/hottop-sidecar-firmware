#pragma once

#include <driver/gpio.h>


/**
 * Returns true if enabled
 */
bool digital_input_get_level(gpio_num_t gpio);


/**
 * Associated digital input pin
 * @param gpio
 */
void digital_input_init(gpio_num_t gpio);

