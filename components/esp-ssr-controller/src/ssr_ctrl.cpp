#include "ssr_ctrl.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>

#define TAG "SSR"

#define MIN_DUTY    1
#define MAX_DUTY    99

/*
 * Specifies the minimum number of periods acceptable for either on or off signals.
 * That is, if mains is 50Hz and this is set to 2, we are saying that the controller will output
 * an ON or OFF signal that will last at least 2 or greater periods, e.g. 25Hz
 */
#define MIN_PERIODS 2

struct duty_packets_t {
  uint16_t high_level;
  uint16_t low_level;
};

struct ssr_ctrl_t {
  ssr_ctrl_config_t cfg;
  gptimer_handle_t timer_handle;
  int duty;
  int level = false;
  uint8_t in_state_count;
};

/**
 * Duty for packets, this only needs to be generated once.
 */
static duty_packets_t _duty_map[MAX_DUTY - MIN_DUTY + 1];

static IRAM_ATTR bool _on_ssr_alarm_cb(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *user_ctx) {
  auto inst = (ssr_ctrl_handle_t) (user_ctx);

  if (inst->duty <= 0) {
    inst->level = 0;
  } else if (inst->duty >= 100) {
    inst->level = 1;
  } else {
    // The idea is to count how many periods we stay in, then once it reaches the desired count we flip
    // to the opposite level and start counting again for that expected count
    int target_count;
    if (inst->level) {
      target_count = _duty_map[inst->duty - MIN_DUTY].high_level;
    } else {
      target_count = _duty_map[inst->duty - MIN_DUTY].low_level;
    }

    if (inst->in_state_count >= target_count) {
      inst->level = !inst->level;
      inst->in_state_count = 0;
    }
  }

  gpio_set_level(inst->cfg.gpio, inst->level);
  inst->in_state_count++;
  return true;
}

/*
 * Turns power off immediately to ssr
 */
esp_err_t ssr_ctrl_power_off(ssr_ctrl_handle_t inst) {
  ESP_RETURN_ON_FALSE(inst, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  // Turn off RMT and force pin to zero as safety
  ESP_ERROR_CHECK(gptimer_stop(inst->timer_handle));
  ssr_ctrl_set_duty(inst, 0);
  ESP_ERROR_CHECK(gpio_set_level(inst->cfg.gpio, 0));
  ESP_LOGI(TAG, "SSR power OFF for gpio %d", inst->cfg.gpio);

  return ESP_OK;
}

esp_err_t ssr_ctrl_power_on(ssr_ctrl_handle_t inst) {
  ESP_RETURN_ON_FALSE(inst, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  ESP_LOGI(TAG, "SSR power ON for gpio %d", inst->cfg.gpio);
  ESP_ERROR_CHECK(gptimer_start(inst->timer_handle));

  return ESP_OK;
}

esp_err_t ssr_ctrl_set_duty(ssr_ctrl_handle_t inst, int duty) {
  ESP_RETURN_ON_FALSE(inst, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  if (duty > 100) {
    duty = 100;
  } else if (duty < 0) {
    duty = 0;
  }

  inst->duty = duty;
  return ESP_OK;
}

esp_err_t ssr_ctrl_get_duty(ssr_ctrl_handle_t inst, int &duty) {
  ESP_RETURN_ON_FALSE(inst, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  duty = inst->duty;
  return ESP_OK;
}

/* Generates an indexed array of duty for low and high cycle times */
void _generate_duty_map() {
  static bool duty_generated = false;

  if (!duty_generated) {
    ESP_LOGI(TAG, "Generating duty map");

    for (int duty = MIN_DUTY; duty <= MAX_DUTY; duty++) {
      int i = duty - MIN_DUTY;
      _duty_map[i].high_level = duty;
      _duty_map[i].low_level = 100 - duty;

      // Use a diviser to simplify the number of periods overall where possible
      for (int j = i; j > 1; j--) {
        if (_duty_map[i].high_level % j == 0 and _duty_map[i].low_level % j == 0) {
          _duty_map[i].high_level = _duty_map[i].high_level / j;
          _duty_map[i].low_level = _duty_map[i].low_level / j;
        }
      }

      // honour the minimum period desired
      if (_duty_map[i].high_level < MIN_PERIODS || _duty_map[i].low_level < MIN_PERIODS) {
        _duty_map[i].high_level *= MIN_PERIODS;
        _duty_map[i].low_level *= MIN_PERIODS;
      }

      ESP_LOGD(TAG, "duty=%d, H=%d, L=%d", duty, _duty_map[i].high_level, _duty_map[i].low_level);
    }

    duty_generated = true;
  }
}

static esp_err_t ssr_ctrl_destroy(ssr_ctrl_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  if (handle->timer_handle) {
    gptimer_disable(handle->timer_handle);
    gptimer_del_timer(handle->timer_handle);
  }
  free(handle);
  return ESP_OK;
}

esp_err_t ssr_ctrl_del(ssr_ctrl_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  // recycle memory resource
  ESP_RETURN_ON_ERROR(ssr_ctrl_destroy(handle), TAG, "destory SSR controller failed");
  return ESP_OK;
}


esp_err_t ssr_ctrl_new(ssr_ctrl_config_t cfg, ssr_ctrl_handle_t *ret_handle) {
  esp_err_t ret = ESP_OK;

  // Use to get even number for 50Hz and 60Hz signals from based Clock
  static int diviser = 14;

  gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_XTAL,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = uint32_t(cfg.mains_hz) * diviser,
      .flags = {.intr_shared = 1}
  };
  gptimer_event_callbacks_t cbs = {
      .on_alarm = _on_ssr_alarm_cb,
  };
  gptimer_alarm_config_t alarm_config = {
      .alarm_count = (uint32_t) diviser / (2),
      .reload_count = 0,
      .flags = {.auto_reload_on_alarm = true},
  };

  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (
      (1ULL << cfg.gpio)
  );
  io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

  ESP_LOGI(TAG, "Creating new controller for GPIO %d", cfg.gpio);

  ssr_ctrl_t *handle;
  // Do allocation for handle and go
  ESP_GOTO_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
  handle = (ssr_ctrl_t *) heap_caps_calloc(1, sizeof(ssr_ctrl_t), MALLOC_CAP_DEFAULT);
  ESP_GOTO_ON_FALSE(handle, ESP_ERR_NO_MEM, err, TAG, "no mem for ssr ctrl");

  // Store config
  handle->cfg = cfg;
  _generate_duty_map();
  ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Failed to configure GPIO");

  // Set up timer now
  ESP_GOTO_ON_ERROR(gptimer_new_timer(&timer_config, &handle->timer_handle), err, TAG, "Failed to configure GPIO");
  ESP_GOTO_ON_ERROR(gptimer_set_alarm_action(handle->timer_handle, &alarm_config), err, TAG, "Failed to set alarm");
  ESP_GOTO_ON_ERROR(gptimer_register_event_callbacks(handle->timer_handle, &cbs, handle), err, TAG,
                    "Failed to set callback");
  ESP_GOTO_ON_ERROR(gptimer_enable(handle->timer_handle), err, TAG, "Could not enable timer");

  ssr_ctrl_set_duty(handle, 0);
  handle->level = 0;

  // Good to go
  *ret_handle = handle;
  return ret;

  err:
  if (ret_handle) {
    ssr_ctrl_destroy(handle);
  }
  return ret;
}