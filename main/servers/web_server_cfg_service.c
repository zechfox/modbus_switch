#include <sys/param.h>
#include <cJSON.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_http_server.h>
#include "esp_http_server_ext.h"

#include "web_server_cfg_service.h"
#include "configuration_adapter.h"

static const char *TAG="APP";

esp_err_t web_srv_cfg_service(httpd_req_t *req) {
    char* resp = NULL;
    char param[128];
    char* buf = NULL;

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*) malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "method", param, sizeof(param)) == ESP_OK) {
                if (strcmp(param, "set") == 0) {
                    if (httpd_query_key_value(buf, "field", param, sizeof(param)) == ESP_OK) {
                        enum cfg_data_idt cfg_id = cfg_adp_id_from_name(param);
                        if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
                            if (cfg_adp_set_by_id_from_raw(cfg_id, param) == ESP_OK) {
                                ESP_LOGI(TAG, "Set %s to %s", cfg_adp_name_from_id(cfg_id), param);
                                resp = (char*)req->user_ctx;
                            }
                        }
                    }
                } else if (strcmp(param, "get") == 0) {
                    if (httpd_query_key_value(buf, "field", param, sizeof(param)) == ESP_OK) {
                        enum cfg_data_idt cfg_id = cfg_adp_id_from_name(param);
                        if (cfg_adp_get_by_id_to_readable(cfg_id, param, sizeof(param)) == ESP_OK) {
                            resp = param;
                        }
                    }
                }
            }
        }
    }

    if (resp == NULL) {
        httpd_resp_set_status(req, HTTPD_400);
        httpd_resp_send(req, HTTPD_400, strlen(HTTPD_400));
    } else {
        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_send(req, resp, strlen(resp));
    }

    if (buf != NULL)
        free(buf);
    return ESP_OK;
}

