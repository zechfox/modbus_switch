#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "mbcontroller.h"       // for mbcontroller defines and api
#include "modbus_params.h"      // for modbus parameters structures

#include "wifi_handler.h"
#include "configuration_adapter.h"
#include "switch_adapter.h"
#include "modbus_tcp_server.h"

#define SLAVE_TAG "modbus tcp slave"

QueueHandle_t s_modbus_event_queue = NULL;

static void modbus_server_got_ip(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  ESP_LOGI(SLAVE_TAG, "Modbus slave is got IP: [%s]. Start Modbus server.", ip4addr_ntoa(&event->ip_info.ip));
  modbus_tcp_server_setup();
  modbus_tcp_server_setup_reg_data(); // Set values into known state
}

void modbus_tcp_server_start()
{
  modbus_tcp_server_init();
  xTaskCreate(modbus_tcp_distribute_event_task, "modbus_tcp_distribute_event_task", 2048, NULL, 2, NULL);
  xTaskCreate(modbus_tcp_switch_task, "modbus_tcp_switch_task", 2048, NULL, 2, NULL);
  ESP_LOGI(SLAVE_TAG, "Modbus slave is initialized.");
}

void modbus_tcp_server_init()
{
  void* mbc_slave_handler = NULL;
  if (NULL == s_modbus_event_queue)
  {
    if (!(s_modbus_event_queue = xQueueCreate(MB_EVENT_QUEUE_SIZE,sizeof(modbus_event_t))))
    {
      ESP_LOGE(SLAVE_TAG, "Create Modbus Event Queue failed.");
    }
  }
  ESP_ERROR_CHECK(mbc_slave_init_tcp(&mbc_slave_handler)); // Initialization of Modbus controller
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &modbus_server_got_ip, NULL));
  ESP_ERROR_CHECK(mbc_slave_start());
}

void modbus_tcp_server_setup()
{
  wifi_mode_t wifi_mode;
  mb_communication_info_t comm_info = { 0 };
  comm_info.ip_port = MB_TCP_PORT_NUMBER;
#if !CONFIG_EXAMPLE_CONNECT_IPV6
  comm_info.ip_addr_type = MB_IPV4;
#else
  comm_info.ip_addr_type = MB_IPV6;
#endif
  comm_info.ip_mode = MB_MODE_TCP;
  comm_info.ip_addr = NULL;
  void * nif = NULL;
  ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode));
  ESP_ERROR_CHECK(tcpip_adapter_get_netif(WIFI_MODE_AP == wifi_mode, &nif));
  comm_info.ip_netif_ptr = nif;
  // Setup communication parameters and start stack
  ESP_ERROR_CHECK(mbc_slave_setup((void*)&comm_info));
  ESP_LOGI(SLAVE_TAG, "Modbus slave is setup.");
}
 
void modbus_tcp_distribute_event_task(void* param)
{
  ESP_LOGI(SLAVE_TAG, "Start Modbus Distribute Event task...");
  modbus_event_t modbus_event;

  if(NULL == s_modbus_event_queue)
  {
    ESP_LOGE(SLAVE_TAG, "Can't find Modbus event queue.");
    vTaskDelete( NULL );
    return;
  }

  while(1)
  {
    // Check for read/write events of Modbus master for certain events
    modbus_event.mb_event = mbc_slave_check_event(MB_READ_WRITE_MASK);
    ESP_ERROR_CHECK(mbc_slave_get_param_info(&modbus_event.mb_params, MB_PAR_INFO_GET_TOUT));
    xQueueSend(s_modbus_event_queue,(void *)&modbus_event,(TickType_t )MB_EVENT_QUEUE_TOUT);
  }

  ESP_LOGE(SLAVE_TAG, "Modbus Distribute Event task exits...");
  vTaskDelete( NULL );
}

static void update_switch_register(uint8_t sw_index, bool status)
{
  // it may conficts with modbus read operation.
  portENTER_CRITICAL();
  coil_reg_params.coils_port0 = (coil_reg_params.coils_port0 & ~(1UL << sw_index)) | (status << sw_index);
  portEXIT_CRITICAL();
  ESP_LOGI(SLAVE_TAG, "COILS Register changed: %d, by [Switch %d Status %d].",
           coil_reg_params.coils_port0,
           sw_index, status);
}

static bool get_coil_status(uint8_t sw_index)
{
  return (coil_reg_params.coils_port0 >> sw_index) & 1U;

}

void modbus_tcp_switch_task(void* param)
{
  ESP_LOGI(SLAVE_TAG, "Start Modbus Switch task...");
  modbus_event_t modbus_event;

  if(NULL == s_modbus_event_queue)
  {
    ESP_LOGE(SLAVE_TAG, "Can't find Modbus event queue. Exit task.");
    vTaskDelete( NULL );
    return;
  }

  while (1)
  {
    BaseType_t status = xQueueReceive(s_modbus_event_queue, &modbus_event, pdMS_TO_TICKS(MB_EVENT_QUEUE_TOUT)); 
    if (status == pdTRUE)
    {
      if (modbus_event.mb_event & MB_EVENT_COILS_RD)
      {
        ESP_LOGI(SLAVE_TAG, "COILS READ (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
                 (uint32_t)modbus_event.mb_params.time_stamp,
                 (uint32_t)modbus_event.mb_params.mb_offset,
                 (uint32_t)modbus_event.mb_params.type,
                 (uint32_t)modbus_event.mb_params.address,
                 (uint32_t)modbus_event.mb_params.size);

      }
      else if (modbus_event.mb_event & MB_EVENT_COILS_WR) {
        ESP_LOGI(SLAVE_TAG, "COILS WRITE (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
                 (uint32_t)modbus_event.mb_params.time_stamp,
                 (uint32_t)modbus_event.mb_params.mb_offset,
                 (uint32_t)modbus_event.mb_params.type,
                 (uint32_t)modbus_event.mb_params.address,
                 (uint32_t)modbus_event.mb_params.size);
        uint8_t sw_index = ((uint32_t)modbus_event.mb_params.mb_offset & 0xFF);
        bool sw_status = switch_adapter_chg_sta(sw_index, get_coil_status(sw_index));
        // update modbus register in case it's hold switch
        update_switch_register(sw_index, sw_status);
      }
      else
      {
        ESP_LOGI(SLAVE_TAG, "Unsupported Operation. (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
                 (uint32_t)modbus_event.mb_params.time_stamp,
                 (uint32_t)modbus_event.mb_params.mb_offset,
                 (uint32_t)modbus_event.mb_params.type,
                 (uint32_t)modbus_event.mb_params.address,
                 (uint32_t)modbus_event.mb_params.size);
 
      }
    }
    else
    {
      ESP_LOGD(SLAVE_TAG, "Receive Modbus Event Queue timeout.");
    }
  }
  ESP_LOGE(SLAVE_TAG, "Modbus Switch task exits...");
  vTaskDelete( NULL );
}

void modbus_tcp_server_setup_reg_data(void)
{
  mb_register_area_descriptor_t reg_area; // Modbus register area descriptor structure
  // The code below initializes Modbus register area descriptors
  // for Modbus Holding Registers, Input Registers, Coils and Discrete Inputs
  // Initialization should be done for each supported Modbus register area according to register map.
  // When external master trying to access the register in the area that is not initialized
  // by mbc_slave_set_descriptor() API call then Modbus stack
  // will send exception response for this register area.
  reg_area.type = MB_PARAM_HOLDING; // Set type of register area
  reg_area.start_offset = MB_REG_HOLDING_START; // Offset of register area in Modbus protocol
  reg_area.address = (void*)&holding_reg_params; // Set pointer to storage instance
  reg_area.size = sizeof(holding_reg_params); // Set the size of register storage instance
  ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

  // Initialization of Input Registers area
  reg_area.type = MB_PARAM_INPUT;
  reg_area.start_offset = MB_REG_INPUT_START;
  reg_area.address = (void*)&input_reg_params;
  reg_area.size = sizeof(input_reg_params);
  ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

  // Initialization of Coils register area
  reg_area.type = MB_PARAM_COIL;
  reg_area.start_offset = MB_REG_COILS_START;
  reg_area.address = (void*)&coil_reg_params;
  reg_area.size = sizeof(coil_reg_params);
  ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

  // Initialization of Discrete Inputs register area
  reg_area.type = MB_PARAM_DISCRETE;
  reg_area.start_offset = MB_REG_DISCRETE_INPUT_START;
  reg_area.address = (void*)&discrete_reg_params;
  reg_area.size = sizeof(discrete_reg_params);
  ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

  // Define initial state of parameters
  discrete_reg_params.discrete_input1 = 1;
  discrete_reg_params.discrete_input3 = 1;
  discrete_reg_params.discrete_input5 = 1;
  discrete_reg_params.discrete_input7 = 1;

  holding_reg_params.holding_data0 = 1.34;
  holding_reg_params.holding_data1 = 2.56;
  holding_reg_params.holding_data2 = 3.78;
  holding_reg_params.holding_data3 = 4.90;

  coil_reg_params.coils_port0 = 0x55;
  coil_reg_params.coils_port1 = 0xAA;

  // get switch default value
  conf_t switch_value = {0};
  cfg_adp_get_u8_by_id(CFG_SW_1, (uint8_t*)&switch_value);
  update_switch_register(0, switch_value.sw_status);
  cfg_adp_get_u8_by_id(CFG_SW_2, (uint8_t*)&switch_value);
  update_switch_register(1, switch_value.sw_status);
  cfg_adp_get_u8_by_id(CFG_SW_3, (uint8_t*)&switch_value);
  update_switch_register(2, switch_value.sw_status);

  input_reg_params.input_data0 = 1.12;
  input_reg_params.input_data1 = 2.34;
  input_reg_params.input_data2 = 3.56;
  input_reg_params.input_data3 = 4.78;
  ESP_LOGI(SLAVE_TAG, "Registers is initialized.");
}
