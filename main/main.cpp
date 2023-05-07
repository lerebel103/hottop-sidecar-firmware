#include <esp_log.h>
#include <driver/gpio.h>
#include "control_loop.h"
#include "sync_signal_generator.h"

#define TAG  "main"


extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting heater sidecar");
    gpio_install_isr_service(0);

    // Here for debugging other boards or self, generates a reference 100Hz signal
    //sync_signal_generator_init(GPIO_NUM_34);

    control_loop_init();
    control_loop_run();
}