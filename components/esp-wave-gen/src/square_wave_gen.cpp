#include <freertos/FreeRTOS.h>
#include "square_wave_gen.h"
#include <esp_check.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_attr.h>
#include <portmacro.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <freertos/semphr.h>

#define TAG "sync_signal"

// 1MHz
#define RESOLUTION 1000000

static square_wave_cfg_t s_cfg;

static SemaphoreHandle_t semaphoreHandle;
static gptimer_handle_t gptimer = nullptr;
static bool _go = false;
static uint64_t _down_edge = 0;

bool  _on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    _down_edge = esp_timer_get_time();
    gpio_set_level(s_cfg.gpio, 0);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(semaphoreHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

    return true;
}

static void _generate_edge(void*) {
    do {
        auto ret = xSemaphoreTake( semaphoreHandle, portMAX_DELAY );
        if (ret == pdPASS) {
            esp_rom_delay_us(s_cfg.low_us - (esp_timer_get_time() - _down_edge));
            gpio_set_level(s_cfg.gpio, 1);
        }
    } while (_go);
}

void square_wave_gen_init(square_wave_cfg_t cfg) {
    s_cfg = cfg;
    
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << s_cfg.gpio)
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
            .alarm_count = s_cfg.period_us,
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
    semaphoreHandle = xSemaphoreCreateBinary();
    xTaskCreate(_generate_edge, "generateEdge", 1024, nullptr, 5, nullptr );
}