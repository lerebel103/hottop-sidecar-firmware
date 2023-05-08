#pragma once

#include <hal/gpio_types.h>

/**
 * Handle for an instance of wave generator
 */
typedef struct square_wave_t *square_wave_handle_t;


struct square_wave_cfg_t {
    gpio_num_t gpio;            ///< GPIO to which the signal is to be output
    uint32_t period_us;         ///< Signal period
    uint32_t low_us;            ///< duration of the low signal portion, must be <period_us
};

/**
 * Starts the generator
 * @param handle Instance of the generator we want to work with
 * @return ESP_OK if no issues, otherwise see gptimer errors
 */
esp_err_t square_wave_gen_start(square_wave_handle_t handle);

/**
 * Stops the generator momentarily
 * @param handle Instance of the generator we want to work with
 * @return ESP_OK if no issues, otherwise see gptimer errors
 */
esp_err_t square_wave_gen_stop(square_wave_handle_t handle);

/**
 * Mimics the hottop "zero" signal that can be used as input for testing with a real controller panel.
 * @param cfg Desired configuration to take
 * @param ret_handle Handled allocated as a result of creating a new instance of this generator
 * @return ESP_OK if all goes well.
 */
esp_err_t square_wave_gen_new(square_wave_cfg_t cfg, square_wave_handle_t *ret_handle);

/**
 * Frees up allocated resources
 * @param handle Instance of the generator we want to work with
 * @return ESP_OK when successful
 */
esp_err_t square_wave_gen_del(square_wave_handle_t handle);
