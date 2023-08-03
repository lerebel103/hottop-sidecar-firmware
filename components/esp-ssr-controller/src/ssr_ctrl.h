#pragma once

#include <hal/gpio_types.h>
#include <cstdint>
#include <esp_err.h>

enum main_hertz_t {
  MAINS_50_HZ = 50,
  MAINS_60_HZ = 60,
};

/**
 * Handle for an instance of an SSR controller
 */
typedef struct ssr_ctrl_t *ssr_ctrl_handle_t;

struct ssr_ctrl_config_t {
  /**
   * GPIO to which this controller will output a signal
   */
  gpio_num_t gpio;

  /**
   * Desired modulation frequency, e.g. which mains frequency is required
   */
  main_hertz_t mains_hz;
};

/**
 * Sets a desired duty, as an integer from [0-100]
 * @param handle controller instance
 * @param duty Desired duty. Note that for safety, this value is trimmed to [0, 100]
 */
esp_err_t ssr_ctrl_set_duty(ssr_ctrl_handle_t handle, int duty);

/**
 * Current duty set.
 * @param handle controller instance
 * @return Integer in the range of [0, 100]
 */
esp_err_t ssr_ctrl_get_duty(ssr_ctrl_handle_t handle, int &duty);

/**
 * Powers off the controller and sets output duty to zero.
 * @param handle controller instance
 */
esp_err_t ssr_ctrl_power_off(ssr_ctrl_handle_t handle);

/**
 * Enables the controller.
 * @param handle controller instance
 */
esp_err_t ssr_ctrl_power_on(ssr_ctrl_handle_t handle);

/**
 * Initialises a controller for an SSR on a given GPIO
 * @param cfg Configuration set desired
 * @param ret_handle Allocates a handle and returns a new instance
 */
esp_err_t ssr_ctrl_new(ssr_ctrl_config_t cfg, ssr_ctrl_handle_t *ret_handle);

/**
 * Frees previously allocated resources.
 * @param handle
 * @return
 */
esp_err_t ssr_ctrl_del(ssr_ctrl_handle_t handle);
