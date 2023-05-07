#include <freertos/FreeRTOS.h>
#include "sync_signal_generator.h"
#include <esp_check.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_attr.h>
#include <portmacro.h>
#include <freertos/task.h>
#include <esp_timer.h>

#define TAG "sync_signal"

// 1MHZ
#define RESOLUTION 1000000
// 480us down edge
#define DOWN_EDGE_WIDTH_US 480


static gptimer_handle_t gptimer = nullptr;
static gpio_num_t s_gpio;
static TaskHandle_t xTaskToNotify = nullptr;
static bool _go = false;
static uint64_t _down_edge = 0;

bool  _on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    _down_edge = esp_timer_get_time();
    gpio_set_level(s_gpio, 0);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    configASSERT(xTaskToNotify != NULL);
    vTaskNotifyGiveFromISR(xTaskToNotify, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

    return true;
}

static void _generate_edge(void*) {
    do {
        auto ulNotificationValue = ulTaskNotifyTake(true, 10000);
        if (ulNotificationValue == 1) {
            esp_rom_delay_us(DOWN_EDGE_WIDTH_US - (esp_timer_get_time() - _down_edge));
            gpio_set_level(s_gpio, 1);
        }
    } while (_go);
}

void sync_signal_generator_init(gpio_num_t gpio) {
    s_gpio = gpio;
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << gpio)
    );
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Set up timer now
    gptimer_config_t timer_config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = RESOLUTION,
            .flags = {.intr_shared = 1}
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
            .alarm_count = 10000, // 10ms
            .reload_count = 0,
            .flags = {.auto_reload_on_alarm = true},
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    gptimer_event_callbacks_t cbs = {
            .on_alarm = _on_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, nullptr));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    _go = true;
    xTaskCreate(_generate_edge, "generateEdge", 672, nullptr, 5, &xTaskToNotify );
}