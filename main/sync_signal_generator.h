#pragma once

#include <hal/gpio_types.h>
#include <hal/rmt_types.h>


/**
 * Mimics the hottop "zero" signal that can be used as input for testing with a real controller panel.
 * @param gpio GPIO for which the signal is wanted (output)
 */
void sync_signal_generator_init(gpio_num_t gpio);
