#pragma once


#include <cstdint>
#include <hal/gpio_types.h>

#define MAX31850_TC_STATUS_OK           0x0
#define MAX31850_TC_STATUS_OPEN_CIRCUIT 0x1
#define MAX31850_TC_STATUS_SHORT_GND    0x2
#define MAX31850_TC_STATUS_SHORT_VCC    0x4

#define MAX31850_NUM_DEVICES_MAX 8

struct max3185_devices_t {
  uint64_t devices_address[MAX31850_NUM_DEVICES_MAX];
  uint8_t devices_address_length;
};

struct max31850_data_t {

  /**
   * Tells if a read was successful and no CRC errors were encountered.
   * The temperature values are undefined when this flag is false.
   */
  bool is_valid;

  /**
   * Bitfield flag representing the status of the thermocouple.
   * MAX31850_TC_STATUS_OK returned when operating correctly.
   * See MAX31850_TC_STATUS_*
   */
  uint8_t thermocouple_status;

  /**
   * Temperature read from the connected thermocouple.
   */
  float tc_temp;

  /**
   * Temperature read from the onboard sensor, representing the junction temperature.
   */
  float junction_temp;
};

/**
 * Reads the current status and temperature data.
 * @param one_wire_pin Pin on which the 1-wire device is attached.
 * @param device_address 1-wire address for the desired device
 * @return
 */
max31850_data_t max31850_read(gpio_num_t one_wire_pin, uint64_t device_address);

/**
 * Lists the number of max31850 devices found.
 * @param one_wire_pin Pin on which 1-wire devices are attached to.
 */
max3185_devices_t max31850_list(gpio_num_t one_wire_pin);
