#include "ssr_ctlr.h"

extern "C" {
#include <driver/rmt.h>
#include "rmt_duty_map.h"
}

#include <esp_log.h>
#define TAG "SSR1"

#define RMT_CLK_DIV 160

/*
 * Turns power off immediately to ssr
 */
extern "C"  void ssr_ctlr_power_off(ssr_ctrl_t& inst) {
    // Turn off RMT and force pin to zero as safety
    ssr_ctlr_set_duty(inst, 0);
    rmt_tx_stop(inst.channel);
    gpio_set_level(inst.gpio, 0);
}

extern "C"  void ssr_ctlr_power_on(ssr_ctrl_t& inst) {
    rmt_tx_start(inst.channel, true);
}

extern "C" void ssr_ctlr_set_duty(ssr_ctrl_t& inst, int duty) {
    if (duty > 100) {
        duty = 100;
    } else if (duty < 0) {
        duty = 0;
    }

    const struct rmt_pulse_t *pulses = rmt_duty_get_pulses(duty, inst.mains_hz);
    ESP_ERROR_CHECK(rmt_fill_tx_items(inst.channel, pulses->items, pulses->num_items, false));
    inst.duty = duty;
}

extern "C" int ssr_ctlr_get_duty(ssr_ctrl_t& inst) {
    return inst.duty;
}

/*
 * Initialize the RMT Tx channel
 */
static void _rmt_tx_init(ssr_ctrl_t& inst) {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(inst.gpio, inst.channel);

    // Disable carrier and enable loop back so we can generate pulses
    config.tx_config.carrier_en = false;
    config.tx_config.loop_en = false;
    config.tx_config.idle_output_en = true;

    // set the maximum clock divider to be able to output
    // RMT pulses in range of about one hundred milliseconds
    config.clk_div = RMT_CLK_DIV;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // Set zero duty and enable loop so we continuously tx the last duty pulses
    ssr_ctlr_set_duty(inst, 0);
    rmt_set_tx_loop_mode(config.channel, true);
}

void ssr_ctlr_init(ssr_ctrl_t& inst) {
    _rmt_tx_init(inst);
    ESP_LOGI(TAG, "SSR initialised for GPIO %d", inst.gpio);
    ssr_ctlr_power_on(inst);
}