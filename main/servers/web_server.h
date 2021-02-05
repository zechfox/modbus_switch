#pragma once
#include <esp_http_server.h>
#include "esp_http_server_ext.h"

#include "web_server_cfg_service.h"

// cJSON helpers
// This method assumes the name is a literal, immutable during the entire lifecycle.
#define cJSON_AddStringToObjectCS(object, name, value) (cJSON_AddItemToObjectCS(object, name, cJSON_CreateString(value)))

// server API
esp_err_t web_server_start(void);
void web_server_stop(void);

esp_err_t web_srv_rst_service(httpd_req_t *req);
esp_err_t web_srv_index_service(httpd_req_t *req);
esp_err_t web_srv_json_get_service(httpd_req_t *req);
esp_err_t web_srv_json_post_service(httpd_req_t *req);
void restart_task(void* param);

