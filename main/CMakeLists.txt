set(PROJECT_NAME "modbus_switch")

idf_component_register(SRCS "modbus_switch_main.c" "configuration_adapter.c" "switch_adapter.c" "web_server_cfg_service.c" "web_server_fota_service.c" "modbus_tcp_server.c" "web_server.c" "wifi_handler.c" "esp_http_server_ext.c"
                       EMBED_TXTFILES "index.html"
                       INCLUDE_DIRS "." "adapters" "servers")
