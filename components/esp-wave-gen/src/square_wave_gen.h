#pragma once

#include <hal/gpio_types.h>
#include <hal/rmt_types.h>

struct square_wave_cfg_t {
    gpio_num_t gpio;
    uint32_t period_us;
    uint32_t low_us;
};

/**
 * Mimics the hottop "zero" signal that can be used as input for testing with a real controller panel.
 * @param gpio GPIO for which the signal is wanted (output)
 */
void square_wave_gen_init(square_wave_cfg_t cfg);
