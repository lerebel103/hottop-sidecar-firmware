#pragma once

#include <hal/gpio_types.h>
#include <cstdint>
#include <esp_err.h>

/**
 * Handle for an instance of wave generator
 */
typedef struct input_pwm_t *input_pwm_handle_t;

enum pwm_on_edge_t {
    /**
     * Calculates PWM assuming an up edge and signal high is ON
     */
    PWM_INPUT_UP_EDGE_ON,

    /**
     * Calculates PWM assuming a down edge and signal low is ON
     */
    PWM_INPUT_DOWN_EDGE_ON
};


struct input_pwm_cfg_t {
    /**
     * GPIO to read input PWM signal from
     */
    gpio_num_t gpio;

    /**
     * Tells how the duty will be calculate, knowing if ON is high or low signal
     */
    pwm_on_edge_t edge_type;

    /**
     * PWM minimum period in microseconds.
     * The period doesn't need to be exact, but must be greater than that of the input signal for the
     * calculations to work. That is, if we have no edges detected in the signal (0 or 100% duty), this
     * value is used as a timeout internally, but also to gauge the minimum time we expect to see the next edge.
     */
    uint32_t period_us;
};

/**
 * Returns the last calculated PWM duty
 *
 * @param handle Handle of the PWM instance that will be allocated.
 * @param duty returned duty
 * @return ESP_OK when all is well
 */
esp_err_t input_pwm_get_duty(input_pwm_handle_t handle, uint8_t &duty);

/**
 * Reads the main power PWM signal via interrupt, to work out an effective duty.
 * Note that the max period of the input signal must be less than 5s.
 *
 * @param cfg Configuration desired for this instance
 * @param handle Handle of the PWM instance that will be allocated.
 */
esp_err_t input_pwm_new(input_pwm_cfg_t cfg, input_pwm_handle_t *ret_handle);

/**
 * Frees all resources.
 * @param handle Handle of the PWM instance that was previously allocated.
 * @return ESP_OK on successful completion
 */
esp_err_t input_pwm_del(input_pwm_handle_t handle);
