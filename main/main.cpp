extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#include <driver/gpio.h>
#include "max31850.h"
#include "ssr_ctlr.h"
#include "sync_signal_generator.h"
#include "control_loop.h"

#define TAG  "main"




extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");
    gpio_install_isr_service(0);

    // Here for debugging other boards or self, generates a reference 100Hz signal
    sync_signal_generator_init(GPIO_NUM_34, RMT_CHANNEL_3);

    control_loop_init();
    control_loop_run();
}