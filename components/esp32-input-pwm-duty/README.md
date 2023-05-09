# Input PWM duty calculator

Reads an input Pulse Width Modulation (PWM) signal on a given input pin and calculates the effective signal
duty by returning an integer in the range of [0-100]%.

This project is written as an `esp-idf` component and can be pulled into your own project (suggest git submodule).

# Usage
## Example
```c
  // Handle for instance
  input_pwm_handle_t pwm_in;

  // Example configuration to read PWM from GPIO6
  input_pwm_cfg_t cfg = {
      .gpio=GPIO_NUM_6, 
      .edge_type=PWM_INPUT_DOWN_EDGE_ON,  // We are expecting an inverted PWM signal (low is ON)
      .period_us=2100000  // This PWM signal is expected to have a period <2s (really slow PWM) 
  };
  
  // Now create a pwm reader
  ESP_ERROR_CHECK( input_pwm_new(cfg, &pwm_in) );
  
  // Read duty
  uint8_t input_duty;
  ESP_ERROR_CHECK( input_pwm_get_duty(pwm_in, input_duty) );

  ...
  ...


  // Don't forget to clean resources when done
  input_pwm_del(pwm_in);

```

## How does this work?

A selected input pin `input_pwm_cfg_t.gpio` is configured to generate interrupts on both rising and falling edges.
The interrupt handler then simply keeps track of the signal's time in low or high position as configured via
`input_pwm_cfg_t.edge_type`, along with the signal period.

A separate RTOS task is then notified on each period, so a duty can be calculated outside of the interrupt handler
(can't do floating point maths in interrupt handling). This is also where `input_pwm_cfg_t.period_us` comes in, as it 
acts as a timeout period so both 0% and 100% can be calculated when no interrupts are in these cases generated. Hence,
you must select `input_pwm_cfg_t.period_us` to be greater than the signal's period, and up to the max latency you are
willing to leave with, could be seconds depending on your application.
