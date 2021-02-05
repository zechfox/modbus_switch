#pragma once
#include <sys/param.h>
#include <cJSON.h>

#include <esp_http_server.h>
#include "esp_http_server_ext.h"

#define HTTP_GET_ARG_MAXLEN 512
#define HTTP_PARAM_MAXLEN 128

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

// service API
esp_err_t web_srv_cfg_service(httpd_req_t *req);
