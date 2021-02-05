#include <stdio.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_spi_flash.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#ifdef CONFIG_MB_MDNS_IP_RESOLVER
#include "mdns.h"
#endif
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "mbcontroller.h"       // for mbcontroller defines and api
#include "modbus_params.h"      // for modbus parameters structures

#include "wifi_handler.h"
#include "configuration_adapter.h"
#include "switch_adapter.h"
#include "web_server_cfg_service.h"
#include "web_server.h"
#include "modbus_tcp_server.h"

void app_main()
{
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init()); // mDNS Implies tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // configurationServer
  ESP_ERROR_CHECK(web_server_start());
  modbus_tcp_server_start();
  wifi_hdl_start_service();

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  printf("Hardware info: ESP8266 with %d CPU cores, WiFi, ", chip_info.cores);
  printf("silicon revision %d, ", chip_info.revision);
  printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
         (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

}
