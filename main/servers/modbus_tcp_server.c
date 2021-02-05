#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "mbcontroller.h"       // for mbcontroller defines and api
#include "modbus_params.h"      // for modbus parameters structures

#include "wifi_handler.h"
#include "configuration_adapter.h"
#include "switch_adapter.h"
#include "modbus_tcp_server.h"

#define SLAVE_TAG "modbus tcp slave"

void modbus_tcp_server_start()
{
  modbus_tcp_server_init();
  ESP_ERROR_CHECK(mbc_slave_start());
  ESP_LOGI(SLAVE_TAG, "Modbus slave stack initialized.");
  xTaskCreate(modbus_tcp_server_task, "modbus_tcp_server_task", 2048, NULL, 2, NULL);
}

void modbus_tcp_server_init()
{
  void* mbc_slave_handler = NULL;

  ESP_ERROR_CHECK(mbc_slave_init_tcp(&mbc_slave_handler)); // Initialization of Modbus controller

  
  mb_register_area_descriptor_t reg_area; // Modbus register area descriptor structure

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
  // always used in STA mode
  ESP_ERROR_CHECK(tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA, &nif));
  comm_info.ip_netif_ptr = nif;
  // Setup communication parameters and start stack
  ESP_ERROR_CHECK(mbc_slave_setup((void*)&comm_info));

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


  modbus_tcp_server_setup_reg_data(); // Set values into known state
}

void modbus_tcp_server_task(void* param)
{
  ESP_LOGI(SLAVE_TAG, "Start modbus server...");
  mb_param_info_t reg_info; // keeps the Modbus registers access information
  // The cycle below will be terminated when parameter holding_data0
  // incremented each access cycle reaches the CHAN_DATA_MAX_VAL value.
  for(;holding_reg_params.holding_data0 < MB_CHAN_DATA_MAX_VAL;) {
    // Check for read/write events of Modbus master for certain events
    mb_event_group_t event = mbc_slave_check_event(MB_READ_WRITE_MASK);
    const char* rw_str = (event & MB_READ_MASK) ? "READ" : "WRITE";
    // Filter events and process them accordingly
    if(event & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) {
      // Get parameter information from parameter queue
      ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));
      ESP_LOGI(SLAVE_TAG, "HOLDING %s (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
               rw_str,
               (uint32_t)reg_info.time_stamp,
               (uint32_t)reg_info.mb_offset,
               (uint32_t)reg_info.type,
               (uint32_t)reg_info.address,
               (uint32_t)reg_info.size);
      if (reg_info.address == (uint8_t*)&holding_reg_params.holding_data0)
      {
        portENTER_CRITICAL();
        holding_reg_params.holding_data0 += MB_CHAN_DATA_OFFSET;
        if (holding_reg_params.holding_data0 >= (MB_CHAN_DATA_MAX_VAL - MB_CHAN_DATA_OFFSET)) {
          coil_reg_params.coils_port1 = 0xFF;
        }
        portEXIT_CRITICAL();
      }
    } else if (event & MB_EVENT_INPUT_REG_RD) {
      ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));
      ESP_LOGI(SLAVE_TAG, "INPUT READ (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
               (uint32_t)reg_info.time_stamp,
               (uint32_t)reg_info.mb_offset,
               (uint32_t)reg_info.type,
               (uint32_t)reg_info.address,
               (uint32_t)reg_info.size);
    } else if (event & MB_EVENT_DISCRETE_RD) {
      ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));
      ESP_LOGI(SLAVE_TAG, "DISCRETE READ (%u us): ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
               (uint32_t)reg_info.time_stamp,
               (uint32_t)reg_info.mb_offset,
               (uint32_t)reg_info.type,
               (uint32_t)reg_info.address,
               (uint32_t)reg_info.size);
    } else if (event & MB_EVENT_COILS_RD) {
      ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));
      ESP_LOGI(SLAVE_TAG, "COILS READ (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
               (uint32_t)reg_info.time_stamp,
               (uint32_t)reg_info.mb_offset,
               (uint32_t)reg_info.type,
               (uint32_t)reg_info.address,
               (uint32_t)reg_info.size);
      switch_adapter_get_status((uint8_t)reg_info.mb_offset, reg_info.address);
      if (coil_reg_params.coils_port1 == 0xFF) break;
    } else if (event & MB_EVENT_COILS_WR) {
      ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));
      ESP_LOGI(SLAVE_TAG, "COILS WRITE (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
               (uint32_t)reg_info.time_stamp,
               (uint32_t)reg_info.mb_offset,
               (uint32_t)reg_info.type,
               (uint32_t)reg_info.address,
               (uint32_t)reg_info.size);
      uint32_t task_param = (uint32_t) reg_info.mb_offset;
      // switch maybe hold for seconds, create a new task to avoid server task
      // did not sleep.
      xTaskCreate(modbus_tcp_server_coil_task, "modbus_tcp_server_coil_task", 2048, (void*)task_param, 2, NULL);
    }
  }
}

void modbus_tcp_server_coil_task(void* param)
{
  uint8_t sw_index = ((uint32_t)param & 0xFF);

  switch_adapter_chg_sta(sw_index);
  vTaskDelete( NULL );
}

void modbus_tcp_server_setup_reg_data(void)
{
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

  input_reg_params.input_data0 = 1.12;
  input_reg_params.input_data1 = 2.34;
  input_reg_params.input_data2 = 3.56;
  input_reg_params.input_data3 = 4.78;
}
