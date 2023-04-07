#include <driver/rmt.h>
#include "sync_signal_generator.h"
#include "rmt_duty_map.h"

#define RMT_CLK_DIV 160

static rmt_channel_t s_channel;

static const rmt_item32_t s_duty[] = {
        {{{ 240, 0, 4760, 1, }}},
        {{{ 0, 1, 0, 0 }}}
};


static void _start() {
    ESP_ERROR_CHECK(rmt_fill_tx_items(s_channel, s_duty, 2, false));
    rmt_set_tx_loop_mode(s_channel, true);
    rmt_tx_start(s_channel, 1);
}

void sync_signal_generator_init(gpio_num_t gpio, rmt_channel_t channel) {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(gpio, channel);
    s_channel = config.channel;

    // Disable carrier and enable loop back, so we can generate pulses
    config.tx_config.carrier_en = false;
    config.tx_config.loop_en = false;
    config.tx_config.idle_output_en = true;

    // set the maximum clock divider to be able to output
    // RMT pulses in range of about one hundred milliseconds
    config.clk_div = RMT_CLK_DIV;
    //config.flags |= RMT_CHANNEL_FLAGS_AWARE_DFS;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    _start();
}