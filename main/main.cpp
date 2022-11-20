extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#define TAG  "main"

#include "level_shifter.h"
#include "panel_inputs.h"
#include "max31850.h"

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");

    level_shifter_init();
    panel_inputs_init();
    level_shifter_enable(true);
    max31850_init();


}