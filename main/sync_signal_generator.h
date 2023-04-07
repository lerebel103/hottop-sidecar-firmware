#pragma once

#include <hal/gpio_types.h>
#include <hal/rmt_types.h>



void sync_signal_generator_init(gpio_num_t gpio, rmt_channel_t channel);
