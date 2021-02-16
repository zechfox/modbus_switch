#include <stdio.h>

#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "configuration_adapter.h"
#include "switch_adapter.h"

static switch_context_t sw_context[3] = {
  [SW1] = {.sw_gpio_pin = SW_1_GPIO_PIN, .sw_conf.value = 0},
  [SW2] = {.sw_gpio_pin = SW_2_GPIO_PIN, .sw_conf.value = 0},
  [SW3] = {.sw_gpio_pin = SW_3_GPIO_PIN, .sw_conf.value = 0}
};
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

  // get default switch status
  cfg_adp_get_u8_by_id(CFG_SW_1, &sw_context[SW1].sw_conf.value);
  cfg_adp_get_u8_by_id(CFG_SW_2, &sw_context[SW2].sw_conf.value);
  cfg_adp_get_u8_by_id(CFG_SW_3, &sw_context[SW3].sw_conf.value);
  switch_adapter_set_status(SW1, sw_context[SW1].sw_conf.sw_status);
  switch_adapter_set_status(SW2, sw_context[SW2].sw_conf.sw_status);
  switch_adapter_set_status(SW3, sw_context[SW3].sw_conf.sw_status);

}

// this will change run time switch status, and not impact on default status.
esp_err_t switch_adapter_set_status(uint8_t sw_index, enum switch_status status)
{
  if (sw_index < 3)
  {
    if (NULL == sw_context[sw_index].sw_mutex_req)
    {
      sw_context[sw_index].sw_mutex_req = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(sw_context[sw_index].sw_mutex_req, portMAX_DELAY);
    gpio_set_level(sw_context[sw_index].sw_gpio_pin, status);
    sw_context[sw_index].sw_conf.sw_status = status;
    xSemaphoreGive(sw_context[sw_index].sw_mutex_req);
  }
  else
    return ESP_ERR_NOT_SUPPORTED;

  return ESP_OK;
}

static esp_err_t switch_adapter_sw_toggling(uint8_t sw_index)
{
  if (sw_index < 3)
  {
    if (NULL == sw_context[sw_index].sw_mutex_req)
    {
      sw_context[sw_index].sw_mutex_req = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(sw_context[sw_index].sw_mutex_req, portMAX_DELAY);
    sw_context[sw_index].sw_conf.sw_status ^= 0x1;
    gpio_set_level(sw_context[sw_index].sw_gpio_pin, sw_context[sw_index].sw_conf.sw_status);
    xSemaphoreGive(sw_context[sw_index].sw_mutex_req);

  }
  else
    return ESP_ERR_NOT_SUPPORTED;

  return ESP_OK;

}

static void switch_adapter_hold_switch(uint8_t sw_index)
{
  const TickType_t xDelayTimeInTick = pdMS_TO_TICKS(sw_context[sw_index].sw_conf.sw_hold_duration * 1000);

  switch_adapter_sw_toggling(sw_index);
  vTaskDelay(xDelayTimeInTick);
  switch_adapter_sw_toggling(sw_index);
}

bool switch_adapter_chg_sta(uint8_t sw_index, bool sw_status)
{
  if (sw_index >=3 )
    return 0;

  if (TOGGLING == sw_context[sw_index].sw_conf.sw_type)
  {
    switch_adapter_set_status(sw_index, (enum switch_status) sw_status);
  }
  else
  {
    if (0 != sw_context[sw_index].sw_conf.sw_hold_duration
        && sw_status)
    {
      // only high value will trigger hold switch
      switch_adapter_hold_switch(sw_index); 
    }
  }
  return sw_context[sw_index].sw_conf.sw_status; 
}

esp_err_t switch_adapter_get_status(uint8_t sw_index, uint8_t * status)
{
  if (sw_index >=3 )
    return ESP_ERR_NOT_SUPPORTED;

  *status = sw_context[sw_index].sw_conf.sw_status;
  return ESP_OK;
}
