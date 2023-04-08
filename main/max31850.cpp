#include "max31850.h"
#include "onewire.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "max31850"


/* Specific to thermocouple temperature conversion in Celcius, see datasheet for positive and negative ranges */
static float _convertTC(const uint8_t *data) {
    uint16_t raw = (data[1] << 6) | (data[0] >> 2);

    // raw = 0x193; // +100.75C
    // raw = 0x3FFF; // -0.25C
    // raw = 0x3FFC; // -1.0C

    // Check if bit 14 is flipped
    if (raw & 0x2000) {
        // Per datasheet, need to remove value from 0x4000 to reveal the scalar when negative
        return -(float) (0x4000u - raw) * 0.25f;
    } else {
        // Standard positive scale
        return (float) raw * 0.25f;
    }
}

/* Specific to cold junction temperature conversion in Celcius, see datasheet for positive and negative ranges */
static float _convertCJ(const uint8_t *data) {
    uint16_t raw = ((data[3] << 8u | data[2]) >> 4u);

    // raw = 0x190; // +25C
    // raw = 0xFFF; // -0.0625C
    // raw = 0xFF0; // -1.0C

    // Check if bit 12 is flipped
    if (raw & 0x800) {
        // Per datasheet, need to remove value from 0x1000 to reveal the scalar when negative
        return -(float) (0x1000u - raw) * 0.0625f;
    } else {
        // Standard positive scale
        return (float) raw * 0.0625f;
    }
}

max31850_data_t max31850_read(gpio_num_t one_wire_pin, uint64_t device_address) {
    ESP_LOGD(TAG, "Reading from %llu", device_address);
    max31850_data_t results = {};
    results.is_valid = false;

    onewire_power(one_wire_pin);

    // Start conversion
    onewire_reset(one_wire_pin);
    onewire_select(one_wire_pin, device_address);
    onewire_write(one_wire_pin, 0x44);
    vTaskDelay(pdMS_TO_TICKS(250));

    // Read scratchpad
    onewire_reset(one_wire_pin);
    onewire_select(one_wire_pin, device_address);
    onewire_write(one_wire_pin, 0xBE);

    // 9 bytes read
    static int MAX_DATA_COUNT = 9;
    uint8_t data[MAX_DATA_COUNT];
    for (int i = 0; i < MAX_DATA_COUNT; i++) {
        data[i] = onewire_read(one_wire_pin);
    }

    uint8_t crc = onewire_crc8(data, MAX_DATA_COUNT - 1);
    if (crc == data[MAX_DATA_COUNT - 1]) {
        // Temperature is valid
        results.is_valid = true;
        ESP_LOGD(TAG, "CRC is valid, reading status and temperatures");

        // Onboard temperature (cold junction reference)
        results.junction_temp = _convertCJ(data);

        // Now for TC temperature, check fault bit [0]
        if (data[0] & 0x01) {
            ESP_LOGD(TAG, "Fault detected");
            // Onboard temp, lower 4 bits are status bits
            results.thermocouple_status = data[2] & 0x07;
        } else {
            results.thermocouple_temp = _convertTC(data);
            results.thermocouple_status = MAX31850_TC_STATUS_OK;
        }
    } else {
        ESP_LOGE(TAG, "Temperature data invalid");
    }

    onewire_depower(one_wire_pin);
    return results;
}

max3185_devices_t max31850_list(gpio_num_t one_wire_pin) {
    max3185_devices_t devices = {};
    devices.devices_address_length = 0;

    // Enumerate one wire
    onewire_reset(one_wire_pin);
    onewire_power(one_wire_pin);

    onewire_search_t search;
    onewire_search_start(&search);

    while (onewire_search_next(&search, one_wire_pin) != ONEWIRE_NONE &&
           devices.devices_address_length < MAX31850_NUM_DEVICES_MAX) {
        ESP_LOGI(TAG, "Found device 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X, islast=%d",
                 search.rom_no[0], search.rom_no[1], search.rom_no[2],
                 search.rom_no[3], search.rom_no[4], search.rom_no[5],
                 search.rom_no[6], search.rom_no[7], search.last_device_found);

        // 0x3B is the max31850 identifier
        if (search.rom_no[0] == 0x3B) {
            devices.devices_address[devices.devices_address_length] = 0;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[7]) << 56u;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[6]) << 48u;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[5]) << 40u;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[4]) << 32u;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[3]) << 24u;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[2]) << 16u;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[1]) << 8u;
            devices.devices_address[devices.devices_address_length] |= (uint64_t)(search.rom_no[0]);
            devices.devices_address_length++;
        }
    }

    onewire_depower(one_wire_pin);
    ESP_LOGI(TAG, "Found %d MAX38150 devices", devices.devices_address_length);
    return devices;
}
