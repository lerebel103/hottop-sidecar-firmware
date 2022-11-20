#include "max31850.h"
#include "onewire.h"
#include <driver/gpio.h>
#include <esp_log.h>

#define ONEWIRE_PIN GPIO_NUM_2
#define TAG "max31850"

void max31850_init() {
    // Enumerate one wire
    onewire_reset(ONEWIRE_PIN);
    onewire_power(ONEWIRE_PIN);

    onewire_search_t search;
    onewire_search_start(&search);
    while (onewire_search_next(&search, ONEWIRE_PIN) != ONEWIRE_NONE) {
        ESP_LOGI(TAG, "Found device %d %d %d %d %d %d %d %d", search.rom_no[0], search.rom_no[1], search.rom_no[2],
                 search.rom_no[3], search.rom_no[4], search.rom_no[5], search.rom_no[6], search.rom_no[7]);

        // Found device 59 183 111 64 18 20 109 244
    }

    onewire_depower(ONEWIRE_PIN);
}
