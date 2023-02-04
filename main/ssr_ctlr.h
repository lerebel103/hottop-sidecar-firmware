#pragma once

#include <hal/gpio_types.h>
#include <hal/rmt_types.h>

struct ssr_ctrl_t {
    gpio_num_t gpio;
    rmt_channel_t channel;
    int mains_hz;
    int duty;
};

extern "C"  void ssr_ctlr_set_duty(ssr_ctrl_t& inst, int duty);
extern "C"  int ssr_ctlr_get_duty(ssr_ctrl_t& inst);
extern "C" void ssr_ctlr_power_off(ssr_ctrl_t& inst);
extern "C" void ssr_ctlr_power_on(ssr_ctrl_t& inst);

void ssr_ctlr_init(ssr_ctrl_t& inst);