#include "balancer.h"

#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>
#include <esp_adc/adc_cali.h>
#include <esp_log.h>
#include <esp_adc/adc_cali_scheme.h>

#define TAG "balancer"

static const adc_unit_t unit = ADC_UNIT_1;
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;

double balance_read_mv() {
  static int num_readings = 1;

  double level_voltage = 0.0;
  for (int i = 0; i < num_readings; i++) {
    int raw, voltage;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage));
    level_voltage += voltage;
    ets_delay_us(250);
  }

  return level_voltage / num_readings;
}

double balance_read_percent() {
  double balance = balance_read_mv();
  static double max_value = 2400;
  static double min_value = 150;
  balance = ((balance - min_value) / (max_value - min_value)) * 100;
  if (balance < 0) {
    balance = 0;
  } else if (balance > 100) {
    balance = 100;
  }
  return balance;
}

static bool _adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle) {
  adc_cali_handle_t handle = NULL;
  esp_err_t ret = ESP_FAIL;
  bool calibrated = false;

#ifdef ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
  adc_cali_line_fitting_config_t cali_config = {
          .unit_id = unit,
          .atten = atten,
          .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
  if (ret == ESP_OK) {
      calibrated = true;
  }
#endif

#ifdef ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  ESP_LOGI(TAG, "Calibration scheme version is %s", "Curve Fitting");
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = unit,
      .atten = atten,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
  if (ret == ESP_OK) {
    calibrated = true;
  }
#endif

  *out_handle = handle;
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Calibration Success");
  } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
    ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
  } else {
    ESP_LOGE(TAG, "Invalid arg or no memory");
  }

  return calibrated;
}


void balancer_init() {
  //-------------ADC1 Init---------------//
  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = unit,
      .ulp_mode = ADC_ULP_MODE_DISABLE
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

  //-------------ADC1 Config---------------//
  adc_oneshot_chan_cfg_t config = {
      .atten = ADC_ATTEN_DB_11,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config));

  //-------------ADC1 Calibration Init---------------//
  _adc_calibration_init(unit, ADC_ATTEN_DB_11, &adc1_cali_handle);
}
