#include <stdio.h>

#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "configuration_adapter.h"
#include "switch_adapter.h"

static switch_context_t sw_context[3] = {
  [SW1] = {.sw_gpio_pin = SW_1_GPIO_PIN, .sw_conf.value = 0},
  [SW2] = {.sw_gpio_pin = SW_2_GPIO_PIN, .sw_conf.value = 0},
  [SW3] = {.sw_gpio_pin = SW_3_GPIO_PIN, .sw_conf.value = 0}
};

static void switch_time_out(TimerHandle_t timer_handler)
{
  uint32_t sw_id = (uint32_t) pvTimerGetTimerID(timer_handler);
  uint32_t sw_cfg_id[3] = {CFG_SW_1, CFG_SW_2, CFG_SW_3};

  switch_conf_t sw_conf = {0};
  cfg_adp_get_u8_by_id(sw_cfg_id[sw_id], &sw_conf.value);
  // reset to default status
  switch_adapter_set_status(sw_id, sw_conf.conf.sw_status);
  if (NULL != sw_context[sw_id].status_update_callback)
  {
    sw_context[sw_id].status_update_callback(sw_id, sw_conf.conf.sw_status);
  }
}

// TODO: udpate sw_hold_duration when it changed in webUI.
static void switch_configuration_updated()
{

}

void switch_adapter_init()
{
  gpio_config_t io_conf;
  //disable interrupt
  io_conf.intr_type = GPIO_INTR_DISABLE;
  //set as output mode
  io_conf.mode = GPIO_MODE_OUTPUT;
  //bit mask of the pins that you want to set,e.g.GPIO15/16
  io_conf.pin_bit_mask = SW_PIN_SEL;
  //disable pull-down mode
  io_conf.pull_down_en = 0;
  //disable pull-up mode
  io_conf.pull_up_en = 0;
  //configure GPIO with the given settings
  gpio_config(&io_conf);

  switch_configuration_updated();

  // get default switch status
  cfg_adp_get_u8_by_id(CFG_SW_1, &sw_context[SW1].sw_conf.value);
  cfg_adp_get_u8_by_id(CFG_SW_2, &sw_context[SW2].sw_conf.value);
  cfg_adp_get_u8_by_id(CFG_SW_3, &sw_context[SW3].sw_conf.value);
  SW_SET_STATUS(sw_context[SW1].sw_gpio_pin, sw_context[SW1].sw_conf.conf.sw_status);
  SW_SET_STATUS(sw_context[SW2].sw_gpio_pin, sw_context[SW2].sw_conf.conf.sw_status);
  SW_SET_STATUS(sw_context[SW3].sw_gpio_pin, sw_context[SW3].sw_conf.conf.sw_status);

  sw_context[SW1].sw_mutex_req = xSemaphoreCreateMutex();
  sw_context[SW2].sw_mutex_req = xSemaphoreCreateMutex();
  sw_context[SW3].sw_mutex_req = xSemaphoreCreateMutex();

  sw_context[SW1].status_update_callback = NULL;
  sw_context[SW2].status_update_callback = NULL;
  sw_context[SW3].status_update_callback= NULL;

  sw_context[SW1].sw_timer_handler = xTimerCreate("SW1 Timer",
                                                  pdMS_TO_TICKS(sw_context[SW1].sw_conf.conf.sw_hold_duration * 1000),
                                                  pdFALSE,
                                                  (void *)SW1,
                                                  switch_time_out);
  sw_context[SW2].sw_timer_handler = xTimerCreate("SW2 Timer",
                                                  pdMS_TO_TICKS(sw_context[SW2].sw_conf.conf.sw_hold_duration * 1000),
                                                  pdFALSE,
                                                  (void *)SW2,
                                                  switch_time_out);
  sw_context[SW3].sw_timer_handler = xTimerCreate("SW3 Timer",
                                                  pdMS_TO_TICKS(sw_context[SW3].sw_conf.conf.sw_hold_duration * 1000),
                                                  pdFALSE,
                                                  (void *)SW3,
                                                  switch_time_out);

}

void switch_adapter_set_state_update_callback(uint8_t sw_index, void * state_update_callback)
{
  sw_context[sw_index].status_update_callback = state_update_callback;
}

// this will change run time switch status, and not impact on default status.
esp_err_t switch_adapter_set_status(uint8_t sw_index, enum switch_status status)
{
  if (sw_index < 3)
  {
    if (NULL != sw_context[sw_index].sw_mutex_req
        && pdTRUE == xSemaphoreTake(sw_context[sw_index].sw_mutex_req, portMAX_DELAY))
    {
      SW_SET_STATUS(sw_context[sw_index].sw_gpio_pin, status);
      sw_context[sw_index].sw_conf.conf.sw_status = status;
      xSemaphoreGive(sw_context[sw_index].sw_mutex_req);
    }
  }
  else
    return ESP_ERR_NOT_SUPPORTED;

  return ESP_OK;
}

esp_err_t switch_adapter_chg_sta(uint8_t sw_index, bool sw_status)
{
  uint32_t sw_cfg_id[3] = {CFG_SW_1, CFG_SW_2, CFG_SW_3};
  switch_conf_t sw_conf = {0};
  if (sw_index >=3 )
    return 0;

  switch_adapter_set_status(sw_index, (enum switch_status) sw_status);
  if (LIMIT == sw_context[sw_index].sw_conf.conf.sw_type)
  {
    if (pdFALSE != xTimerIsTimerActive(sw_context[sw_index].sw_timer_handler))
    {
      if (pdFAIL == xTimerStop(sw_context[sw_index].sw_timer_handler, 0))
      {
        return ESP_FAIL;
      }
    }
    cfg_adp_get_u8_by_id(sw_cfg_id[sw_index], &sw_conf.value);
    // if switch set to default state, do not start the timer.
    if (((enum switch_status) sw_status) != sw_conf.conf.sw_status)
    {
      if (pdPASS != xTimerStart(sw_context[sw_index].sw_timer_handler, 0))
      {
        return ESP_FAIL;
      }
    }
  }
  return ESP_OK; 
}

esp_err_t switch_adapter_get_status(uint8_t sw_index, uint8_t * status)
{
  if (sw_index >=3 )
    return ESP_ERR_NOT_SUPPORTED;

  *status = sw_context[sw_index].sw_conf.conf.sw_status;
  return ESP_OK;
}
