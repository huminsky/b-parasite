#include <stdbool.h>
#include <stdint.h>

#include "app_timer.h"
#include "ble_advdata.h"
#include "bsp.h"
#include "nordic_common.h"
#include "nrf_delay.h"
#include "nrf_drv_rtc.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_soc.h"
#include "prst/adc.h"
#include "prst/ble.h"
#include "prst/pwm.h"
#include "prst/rtc.h"
#include "prst/shtc3.h"
#include "prst_config.h"

// #define DEAD_BEEF 0xDEADBEEF
// void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
//   app_error_handler(DEAD_BEEF, line_num, p_file_name);
// }

// A small wrap-around counter for deduplicating BLE packets on the receiver.
static uint8_t run_counter = 0;

static void log_init(void) {
  ret_code_t err_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();
  NRF_LOG_INFO("Log inited");
}

static void leds_init(void) {
  nrf_gpio_cfg_output(PRST_LED_PIN);
  NRF_LOG_INFO("Leds inited");
}

#define FPU_EXCEPTION_MASK 0x0000009F

static void power_management_init(void) {
  ret_code_t err_code;
  err_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(err_code);
}

static void power_manage(void) {
  __set_FPSCR(__get_FPSCR() & ~(FPU_EXCEPTION_MASK));
  (void)__get_FPSCR();
  NVIC_ClearPendingIRQ(FPU_IRQn);
  nrf_pwr_mgmt_run();
}

// Here we need to be extra careful with what operations we do. This callback
// has to return fast-ish, otherwise we hit some hard exceptions.
static void rtc_callback() {
  nrf_gpio_pin_set(PRST_LED_PIN);
  prst_shtc3_read_t temp_humi = prst_shtc3_read();
  nrf_gpio_pin_set(PRST_FAST_DISCH_PIN);
  prst_pwm_init();
  prst_pwm_start();
  prst_adc_batt_read_t batt_read = prst_adc_batt_read();
  prst_adc_soil_moisture_t soil_read = prst_adc_soil_read();
  prst_pwm_stop();
  nrf_gpio_pin_clear(PRST_FAST_DISCH_PIN);
  prst_ble_update_adv_data(batt_read.millivolts, temp_humi.temp_millicelcius,
                           temp_humi.humidity, soil_read.relative, run_counter);
  NRF_LOG_FLUSH();
  prst_adv_start();
  nrf_delay_ms(PRST_BLE_ADV_TIME_IN_MS);
  prst_adv_stop();
  nrf_gpio_pin_clear(PRST_LED_PIN);
  NRF_LOG_FLUSH();
  run_counter++;
}

int main(void) {
  log_init();
  nrf_gpio_cfg_output(PRST_FAST_DISCH_PIN);
  leds_init();
  power_management_init();
  prst_ble_init();
  prst_adc_init();
  prst_shtc3_init();

  // Set up RTC. It will call our custom callback at a regular interval, defined
  // by PRST_DEEP_SLEEP_IN_SECONDS.
  prst_rtc_set_callback(rtc_callback);
  prst_rtc_init();

  // In addition to scheduling it, let's immediatelly call it - it makes
  // debugging less tedious.
  rtc_callback();

  // Here we go into a low energy mode. The datasheet calls this mode "System
  // ON", and in my tests it consumes around 2.7uA.
  for (;;) {
    power_manage();
  }
}
