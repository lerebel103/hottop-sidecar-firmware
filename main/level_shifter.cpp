#include <hal/gpio_types.h>
#include <driver/gpio.h>
#include "level_shifter.h"

#define LS_PIN_EN GPIO_NUM_5

void level_shifter_enable(bool enable) {
  gpio_set_level(LS_PIN_EN, !enable ? 0 : 1);
}

void level_shifter_init() {
  // --- Configure input switch that drives the pump
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (
      (1ULL << LS_PIN_EN)
  );

  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  level_shifter_enable(false);
}
