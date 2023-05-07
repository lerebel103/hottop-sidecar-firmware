#pragma once

#include <hal/gpio_types.h>
#include <cstdint>
#include <esp_err.h>

enum main_hertz_t {
    MAINS_50_HZ = 50,
    MAINS_60_HZ = 60,
};

struct ssr_ctrl_t;

/**
 * Handle for an instance of an SSR controller
 */
typedef struct ssr_ctrl_t *ssr_ctrl_handle_t;

struct ssr_ctrl_config_t {
    gpio_num_t gpio;
    main_hertz_t mains_hz;
};

void ssr_ctrl_set_duty(ssr_ctrl_handle_t handle, int duty);
int ssr_ctrl_get_duty(ssr_ctrl_handle_t handle);
void ssr_ctrl_power_off(ssr_ctrl_handle_t handle);
void ssr_ctrl_power_on(ssr_ctrl_handle_t handle);

/**
 * Initialises a controller for an SSR on a given GPIO
 * @param cfg
 * @param ret_handle Allocates a handle and returns a new instance
 */
esp_err_t ssr_ctrl_new(ssr_ctrl_config_t cfg, ssr_ctrl_handle_t* ret_handle);

/**
 * Frees previously allocated resources.
 * @param handle
 * @return
 */
esp_err_t ssr_ctrl_del(ssr_ctrl_handle_t handle);
