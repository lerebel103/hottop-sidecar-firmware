# MAX31850 thermocouple amplifier support on ESP32

Support for [Maxim Integrated's MAX31850](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31850-MAX31851.pdf)
thermocouple amplifier, which makes use of the 1-wire protocol.

This is a component written for `esp-idf`  (tested on v4 & v5) and can be pulled into your own project. It does
have a dependency on [esp32-onewire](https://github.com/lerebel103/esp32-onewire) which will also need to be pulled in.
The implementation follows the datasheet to expose both junction (onboard sensor) and thermocouple
temperatures, along with states. Chips that are wired directly to a power supply are verified to work, however
it's unclear if 1-wire parasitic mode will work (untested), if not small modifications may be needed.

# Usage
```c
    // Start by querying and listing all max31850 attached to GPIO 2 as example
    max3185_devices_t found_devices = max31850_list(GPIO_NUM_2);

    // Then the address of the discovered devices can be used as handle
    if (found_devices.devices_address_length > 0) {
      // Uses the first found device as example
      uint64_t max31850_addr = found_devices.devices_address[0];
      max31850_data_t elm_temp = max31850_read(GPIO_NUM_2, max31850_addr);
      if (elm_temp.is_valid) {
        // CRC is ok 
        if (elm_temp.tc_status == MAX31850_TC_STATUS_OK) {
          // All is well
          ESP_LOGI(TAG, "Thermocouple=%.2fC, Junction=%.2fC", elm_temp.tc_temp, elm_temp.junction_temp);
        } else {
          // Error reported
          if (elm_temp.tc_status & MAX31850_TC_STATUS_OPEN_CIRCUIT) {
            ESP_LOGE(TAG, "thermocouple fault OPEN CIRCUIT");
          } else if (elm_temp.tc_status & MAX31850_TC_STATUS_SHORT_GND) {
            ESP_LOGE(TAG, "thermocouple fault SHORT TO GROUND");
          } else if (elm_temp.tc_status & MAX31850_TC_STATUS_SHORT_VCC) {
            ESP_LOGE(TAG, "thermocouple fault SHORT TO VCC");
          }
        }  
      } else {
          // ... Handle hardware read errors
      }
    } else {
      // ... no max31850
    }
```