#include <driver/gpio.h>
#include <esp_timer.h>
#include "input_power_duty.h"

static gpio_num_t s_gpio;
static int64_t s_on_start = 0;
static int64_t s_period = 0;
static int64_t s_period_on = 0;

static int64_t s_last_on_start = 0;

double input_power_duty_get() {
    // If we have no interrupts, we are either 0% or 100%
    double duty = 0;
    if(s_on_start == s_last_on_start) {
        if (gpio_get_level(s_gpio) == 0) {
            duty = 100;
        }
    } else if (s_period > 0){
        duty = (s_period_on / (double)s_period) * 100;
    }
    s_last_on_start = s_on_start;
    return duty;
}


static void _handle_isr(void *) {
    if (gpio_get_level(s_gpio) == 0) {
        uint64_t on_start = esp_timer_get_time();
        if (s_on_start > 0) {
            s_period = on_start - s_on_start;
        }
        s_on_start = esp_timer_get_time();
    } else {
        s_period_on = esp_timer_get_time() - s_on_start;
    }
}


void input_power_duty_init(gpio_num_t gpio) {
    // --- Configure input switch that drives the pump
    s_gpio = gpio;
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << gpio)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_isr_handler_add(gpio, _handle_isr, nullptr);
}
