#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gptimer.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include "control_loop.h"
#include "ssr_ctlr.h"
#include "max31850.h"
#include "level_shifter.h"
#include "rmt_duty_map.h"
#include "panel_inputs.h"
#include "balancer.h"
#include "input_power_duty.h"
#include "digital_input.h"

// Interval in MHz
#define INTERVAL 2000000
#define TAG "control"

#define ONEWIRE_PIN             GPIO_NUM_2
#define DRUM_MOTOR_SIGNAL_PIN   GPIO_NUM_7

#define MAX_SECONDARY_HEAT_RATIO 0.6f;
#define TEMPERATURE_TC_MAX      270.0f
#define TEMPERATURE_BOARD_MAX   75.0f

static gptimer_handle_t gptimer;
/* Stores the handle of the task that will be notified when the
transmission is complete. */
static TaskHandle_t xTaskToNotify = nullptr;
static bool _go = false;


static ssr_ctrl_t s_ssr1;
static ssr_ctrl_t s_ssr2;
static uint64_t s_max31850_addr = 0;


/*
 * Checks that the TC and environmental temperatures are within acceptable range
 */
static bool _temperature_ok() {
    bool is_ok = false;

    // Get TC value
    max31850_data_t elm_temp = max31850_read(ONEWIRE_PIN, s_max31850_addr);
    if (elm_temp.is_valid) {
        if (elm_temp.thermocouple_status == MAX31850_TC_STATUS_OK) {
            ESP_LOGI(TAG, "Thermocouple=%fC, Board=%fC", elm_temp.thermocouple_temp, elm_temp.junction_temp);

            // We want to make sure we are under max temperatures allowed
            if (elm_temp.thermocouple_temp <= TEMPERATURE_TC_MAX && elm_temp.junction_temp < TEMPERATURE_BOARD_MAX) {
                is_ok = true;
            } else {
                ESP_LOGW(TAG, "Max temperature exceed Max TC=%f, Max Board=%f", TEMPERATURE_TC_MAX, TEMPERATURE_BOARD_MAX);
            }
        } else {
            if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_OPEN_CIRCUIT) {
                ESP_LOGE(TAG, "Unable to run, thermocouple fault OPEN CIRCUIT");
            } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_GND) {
                ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO GROUND");
            } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_VCC) {
                ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO VCC");
            }
        }
    } else {
        ESP_LOGE(TAG, "Unable to run, error reading thermocouple data.");
    }

    return is_ok;
}

/*
 * Ensures conditions are met so we can start controlling
 *
 * The temperatures read must be within allowed ranges.
 * The main drum must be spinning.
 */
static bool _can_control() {
    bool is_ok =  _temperature_ok();

    if(digital_input_get_level(DRUM_MOTOR_SIGNAL_PIN) == 0) {
        is_ok = false;
        ESP_LOGW(TAG, "Drum motor OFF, not applying heat");
    }

    return is_ok;
}

static bool _on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* At this point xTaskToNotify should not be NULL as
    a transmission was in progress. */
    configASSERT(xTaskToNotify != NULL);

    /* Notify the task that the transmission is complete. */
    vTaskNotifyGiveFromISR(xTaskToNotify, &xHigherPriorityTaskWoken);

    /* If xHigherPriorityTaskWoken is now set to pdTRUE then a
    context switch should be performed to ensure the interrupt
    returns directly to the highest priority task.  The macro used
    for this purpose is dependent on the port in use and may be
    called portEND_SWITCHING_ISR(). */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return true;
}

static void _control() {
    double input_duty = input_power_duty_get();
    double output_duty = 0;
    double balance = balance_read_percent();

    if (_can_control()) {
        output_duty = input_duty * balance / 100 * MAX_SECONDARY_HEAT_RATIO;
    }

    ESP_LOGI(TAG, "Input=%f, Balance: %f, Output=%f", input_duty, balance, output_duty);
    ssr_ctlr_set_duty(s_ssr1, input_duty);
    ssr_ctlr_set_duty(s_ssr2, output_duty);

    vTaskDelay(pdMS_TO_TICKS(1750));
}

void control_loop_run() {
    xTaskToNotify = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    _go = true;

    esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms= (int)(INTERVAL * 1.5),
            .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
            .trigger_panic=true
    };

#if !CONFIG_ESP_TASK_WDT_INIT
    ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_cfg));
#endif
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_cfg));
    ESP_ERROR_CHECK(esp_task_wdt_add(xTaskToNotify));

    // We are good to go
    level_shifter_enable(true);

    do {
        auto ulNotificationValue = ulTaskNotifyTake(true, pdMS_TO_TICKS(INTERVAL));
        if (ulNotificationValue == 1) {
            ESP_LOGI(TAG, "event");
            ESP_ERROR_CHECK(esp_task_wdt_reset());
            _control();
        } else {
            ESP_LOGE(TAG, "Hardware timer timeout.");
        }
    } while (_go);

    // We are good to go
    level_shifter_enable(false);
}

void control_loop_stop() {
    ESP_ERROR_CHECK(gptimer_stop(gptimer));
    _go = false;
}

void control_loop_init() {
    level_shifter_init();
    panel_inputs_init();
    balancer_init();
    digital_input_init(DRUM_MOTOR_SIGNAL_PIN);

    // Init reading inbound power duty
    input_power_duty_init(GPIO_NUM_6);

    // Thermocouple amplifier
    max3185_devices_t found_devices = max31850_list(ONEWIRE_PIN);
    ESP_ERROR_CHECK(found_devices.devices_address_length == 1 ? ESP_OK : ESP_FAIL);
    s_max31850_addr = found_devices.devices_address[0];

    // Init SSRs
    s_ssr1 = {.gpio = GPIO_NUM_10, .channel = RMT_CHANNEL_0, .mains_hz= MAINS_50HZ, .duty = 0};
    ssr_ctlr_init(s_ssr1);
    s_ssr2 = {.gpio = GPIO_NUM_11, .channel = RMT_CHANNEL_1, .mains_hz= MAINS_50HZ, .duty = 0};
    ssr_ctlr_init(s_ssr2);

    // Turn off heat
    ssr_ctlr_set_duty(s_ssr1, 0);
    ssr_ctlr_set_duty(s_ssr2, 0);

    // Set up timer now
    gptimer_config_t timer_config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = 1 * 1000 * 1000, // 1MHz, 1 tick = 1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
            .alarm_count = INTERVAL,
            .reload_count = 0,
            .flags = {.auto_reload_on_alarm = true},
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    gptimer_event_callbacks_t cbs = {
            .on_alarm = _on_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, nullptr));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
}