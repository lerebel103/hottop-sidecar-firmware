#pragma once

#include <hal/gpio_types.h>
#include <hal/rmt_types.h>

/**
 * Returns the last computed power duty
 */
double input_power_duty_get();


/**
 * Reads the main power PWM signal via interrupt, to work out an effective duty
 * @param gpio
 */
void input_power_duty_init(gpio_num_t gpio);

