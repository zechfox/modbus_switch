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

#include "web_server.h"
#include "wifi_handler.h"
#include "web_server_cfg_service.h"
#include "web_server_fota_service.h"
#include "configuration_adapter.h"

static httpd_handle_t server = NULL;
#define TAG "webServer"
httpd_uri_t restart = {
    .uri       = "/restart",
    .method    = HTTP_GET,
    .handler   = web_srv_rst_service,
    .user_ctx  = "Restarting now..."
};

httpd_uri_t config_get = {
    .uri       = "/config",
    .method    = HTTP_GET,
    .handler   = web_srv_cfg_service,
    .user_ctx  = "Ok~~~"
};

httpd_uri_t index_get = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = web_srv_index_service 
};

httpd_uri_t json_get = {
    .uri       = "/json_get",
    .method    = HTTP_GET,
    .handler   = web_srv_json_get_service
};

httpd_uri_t json_post = {
    .uri       = "/json_post",
    .method    = HTTP_POST,
    .handler   = web_srv_json_post_service
};

httpd_uri_t fota_post = {
    .uri       = "/fota",
    .method    = HTTP_POST,
    .handler   = web_srv_fota_service
};
// "/restart?confirm=yes", restart the system
void restart_task(void* param) {
    web_server_stop();
    ESP_LOGI(TAG, "Restarting...");
    esp_restart();
}

esp_err_t web_srv_send_rsp(httpd_req_t *req, const char *status, const char * msg, size_t msg_len)
{
  if (NULL != status)
  {
    httpd_resp_set_status(req, status);
  }
  httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
  return httpd_resp_send(req, msg, strlen(msg));
}

esp_err_t web_srv_rst_service(httpd_req_t *req) {
    const char* resp_str = "?confirm=yes not found, restarting aborted.";
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param[4];
        /* Get value of expected key from query string */
        if (httpd_query_key_value(buf, "confirm", param, sizeof(param)) == ESP_OK &&
                strcmp(param, "yes") == 0) {
            resp_str = (const char*) req->user_ctx;

            xTaskCreate(restart_task, "restart_task", 1024, NULL, 6, NULL);
        }
    }

    web_srv_send_rsp(req, NULL, resp_str, strlen(resp_str));
    return ESP_OK;
}

esp_err_t web_srv_index_service(httpd_req_t *req) {
    web_srv_send_rsp(req, NULL, index_html_start, strlen(index_html_start));
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    if (server != NULL)
        return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_get);
        httpd_register_uri_handler(server, &config_get);
        httpd_register_uri_handler(server, &restart);
        httpd_register_uri_handler(server, &json_get);
        httpd_register_uri_handler(server, &json_post);
        httpd_register_uri_handler(server, &fota_post);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return ESP_FAIL;
}

void web_server_stop(void) {
    if (server == NULL) {
        ESP_LOGI(TAG, "Web server is not running.");
        return;
    }

    ESP_LOGI(TAG, "Stopping web server.");
    httpd_stop(server);
    ESP_LOGI(TAG, "Web server stopped.");
}

static void json_get_get_fields(cJSON* resp_root, cJSON* req_array) {
    char param[HTTP_PARAM_MAXLEN];

    if (req_array == NULL)
        return;

    cJSON* req_iterator = NULL;
    cJSON_ArrayForEach(req_iterator, req_array) {
        char* field_name = cJSON_GetStringValue(req_iterator);
        enum cfg_data_idt cfg_id = cfg_adp_id_from_name(field_name);
        if (cfg_adp_get_by_id_to_readable(cfg_id, param, sizeof(param)) == ESP_OK) {
            cJSON_AddStringToObject(resp_root, field_name, param);
        }
    }
}

static const char* json_post_set_fields(cJSON* req_array) {
    const char* ret = HTTPD_200;

    if (req_array == NULL)
        return NULL;

    cJSON* req_iterator = NULL;
    cJSON_ArrayForEach(req_iterator, req_array) {
        char* field_name = req_iterator->string;
        char* field_value = cJSON_GetStringValue(req_iterator);
        enum cfg_data_idt cfg_id = cfg_adp_id_from_name(field_name);
        if (cfg_adp_set_by_id_from_raw(cfg_id, field_value) != ESP_OK) {
            ret = HTTPD_404;
        }
    }
    return ret;
}

static const char* json_post_parser(const cJSON* req) {
    cJSON* req_method_node = cJSON_GetObjectItem(req, "method");
    char* req_method = cJSON_GetStringValue(req_method_node);

    if (strcmp(req_method, "set") == 0) {
        return json_post_set_fields(cJSON_GetObjectItem(req, "fields"));
    }

    return HTTPD_404;
}
static const char* const wifi_sta_status_str[] = {"disconnected", "connecting", "connected"};

static void json_get_wifi_sta_status(cJSON* resp_root) {
    char ssid[WIFI_SSID_MAXLEN];
    ip_info_t ip_info;

    cJSON_AddStringToObjectCS(resp_root, "wifi_sta_status", wifi_sta_status_str[wifi_hdl_sta_query_status()]);

    if (wifi_hdl_sta_query_ap(ssid, sizeof(ssid)) == ESP_OK) {
        cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ap_ssid", ssid);

        if (wifi_hdl_query_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info) == ESP_OK) {
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ip4_address", ip_info.ip4_addr);
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ip4_netmask", ip_info.ip4_netmask);
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ip4_gateway", ip_info.ip4_gateway);

            char* ipv6_addr[IPV6_ADDR_COUNT];
            for (size_t i=0; i<ip_info.ip6_count; i++) {
                ipv6_addr[i] = (char*)&ip_info.ip6_addr[i];
            }
            cJSON_AddItemToObjectCS(resp_root, "wifi_sta_ip6_address",
                                    cJSON_CreateStringArray((const char**)ipv6_addr, ip_info.ip6_count));
        }
    }
}

static void json_get_wifi_connect(cJSON* resp_root, cJSON* req) {
    cJSON* req_item_node;
    char sta_ssid_req[WIFI_SSID_MAXLEN];
    char sta_pass_req[WIFI_PASS_MAXLEN];
    int use_prev_cfg = 0;

    sta_ssid_req[0] = '\0';
    sta_pass_req[0] = '\0';

    req_item_node = cJSON_GetObjectItem(req, "wifi_sta_ssid");
    if (req_item_node != NULL) {
        use_prev_cfg = 1;

        strncpy(sta_ssid_req, cJSON_GetStringValue(req_item_node), WIFI_SSID_MAXLEN);
        // Trucate the string if it is greater than WIFI_SSID_MAXLEN-1
        sta_ssid_req[WIFI_SSID_MAXLEN-1] = '\0';
        cJSON_AddStringToObjectCS(resp_root, "wifi_sta_ssid", sta_ssid_req);

        req_item_node = cJSON_GetObjectItem(req, "wifi_sta_pass");
        if (req_item_node != NULL) {
            strncpy(sta_pass_req, cJSON_GetStringValue(req_item_node), WIFI_PASS_MAXLEN);
            // Trucate the string if it is greater than WIFI_PASS_MAXLEN-1
            sta_pass_req[WIFI_PASS_MAXLEN-1] = '\0';
            cJSON_AddStringToObjectCS(resp_root, "wifi_sta_pass", sta_pass_req);
        }
    }

    cJSON_AddBoolToObject(resp_root, "wifi_sta_use_prev_cfg", use_prev_cfg);
    cJSON_AddBoolToObject(resp_root, "return_value",
                          wifi_hdl_sta_connect(sta_ssid_req, sta_pass_req));
}

static void json_get_wifi_ap_status(cJSON* resp_root) {
    char ssid[WIFI_SSID_MAXLEN];
    ip_info_t ip_info;
    esp_err_t ret = wifi_hdl_ap_query(ssid, WIFI_SSID_MAXLEN);

    cJSON_AddBoolToObject(resp_root, "wifi_ap_turned_on", ret == ESP_OK);

    if (ret == ESP_OK) {
        cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ssid", ssid);

        if (wifi_hdl_query_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info) == ESP_OK) {
            cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ip4_address", ip_info.ip4_addr);
            cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ip4_netmask", ip_info.ip4_netmask);
            cJSON_AddStringToObjectCS(resp_root, "wifi_ap_ip4_gateway", ip_info.ip4_gateway);

            char* ipv6_addr[IPV6_ADDR_COUNT];
            for (size_t i=0; i<ip_info.ip6_count; i++) {
                ipv6_addr[i] = (char*)&ip_info.ip6_addr[i];
            }
            cJSON_AddItemToObjectCS(resp_root, "wifi_ap_ip6_address",
                                    cJSON_CreateStringArray((const char**)ipv6_addr, ip_info.ip6_count));
        }
    }
}

static cJSON* json_get_parser(cJSON* req) {
    // Duplicate "method" field to the response
    cJSON* req_item_node = cJSON_GetObjectItem(req, "method");
    if (req_item_node == NULL) {
        return NULL;
    }
    cJSON* resp_root = cJSON_CreateObject();
    cJSON_DetachItemViaPointer(req, req_item_node);
    cJSON_AddItemToObject(resp_root, "method", req_item_node);
    char* req_method = cJSON_GetStringValue(req_item_node);

    // Copy trans_id
    req_item_node = cJSON_GetObjectItem(req, "trans_id");
    if (req_item_node != NULL) {
        cJSON_DetachItemViaPointer(req, req_item_node);
        cJSON_AddItemToObject(resp_root, "trans_id", req_item_node);
    }

    if (strcmp(req_method, "get") == 0) {
        json_get_get_fields(resp_root, cJSON_GetObjectItem(req, "fields"));
    } else if (strcmp(req_method, "set") == 0) {
        cJSON_AddStringToObjectCS(resp_root, "return_value",
                                json_post_set_fields(cJSON_GetObjectItem(req, "fields")));
    } else if (strcmp(req_method, "wifi_sta_status") == 0) {
        json_get_wifi_sta_status(resp_root);
    } else if (strcmp(req_method, "wifi_sta_connect") == 0) {
        json_get_wifi_connect(resp_root, req);
    } else if (strcmp(req_method, "wifi_sta_disconnect") == 0) {
        wifi_hdl_sta_disconnect();
    } else if (strcmp(req_method, "wifi_ap_on") == 0) {
        cJSON_AddBoolToObject(resp_root, "return_value", wifi_hdl_ap_turn_on());
    } else if (strcmp(req_method, "wifi_ap_off") == 0) {
        cJSON_AddBoolToObject(resp_root, "return_value", wifi_hdl_ap_turn_off());
    } else if (strcmp(req_method, "wifi_ap_status") == 0) {
        json_get_wifi_ap_status(resp_root);
    }

    return resp_root;
}

esp_err_t web_srv_json_get_service(httpd_req_t *req) {
    cJSON* json_req = NULL;
    esp_err_t ret = ESP_OK;
    char* buf = NULL;
    const char* req_str = NULL;
    char* status = NULL;
    char* rsp_msg = NULL;
    size_t buf_len = 0;

    req_str = httpd_req_get_url_query_str_byref(req, &buf_len);
    if (req_str == NULL) {
      status = HTTPD_400; 
      rsp_msg = "query string is not found.";
      goto func_ret;
    }

    // Get HTTP request key-value pair
    req_str = httpd_query_key_value_byref(req_str, "json", &buf_len);
    if (req_str == NULL) {
      status = HTTPD_400; 
      rsp_msg = "query string is not expected.";
      goto func_ret;
    }

    // HTTP request value decode
    if (buf_len > HTTP_GET_ARG_MAXLEN) {
      status = HTTPD_400;
      rsp_msg = "query string is too long.";
        goto func_ret;
    }
    buf = malloc(buf_len + 1);
    buf_len = httpd_query_value_decode(req_str, buf_len, buf);

    // Json Parse
    json_req = cJSON_Parse(buf);
    free(buf);
    buf = NULL;
    if (!json_req) {
        ESP_LOGE("json", "Error before: [%s]", cJSON_GetErrorPtr());
        status = HTTPD_400;
        rsp_msg = "parse json request failed.";
        goto func_ret;
    }

    // Generate and send the response
    cJSON* json_resp = json_get_parser(json_req);
    if (json_resp) {
        buf = cJSON_PrintUnformatted(json_resp);
        cJSON_Delete(json_resp);
        rsp_msg = buf;
    } else {
      status = HTTPD_400;
      rsp_msg = "unknown methods.";
    }

func_ret:
    ret = web_srv_send_rsp(req, status, rsp_msg, strlen(rsp_msg));
    if (json_req)
        cJSON_Delete(json_req);
    if (buf)
        free(buf);
    return ret;
}



esp_err_t web_srv_json_post_service(httpd_req_t *req) {
    cJSON* json_req = NULL;
    esp_err_t ret = ESP_OK;
    char* status = NULL;
    char* buf = NULL;
    char* rsp_msg = NULL;

    size_t remaining = req->content_len;
    if (remaining <= 1 || remaining >= 1024) {
        status = HTTPD_400;
        rsp_msg = "message too long.";
        ret = web_srv_send_rsp(req, status, rsp_msg, strlen(rsp_msg));
        goto func_ret;
    }

    buf = (char*) malloc(remaining);
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf, remaining);
        if (ret > 0) {
            remaining -= ret;
        } else if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* Retry receiving if timeout occurred */
            continue;
        } else {
            ret = ESP_FAIL;
            goto func_ret;
        }
    }

    json_req = cJSON_Parse(buf);
    free(buf);
    buf = NULL;
    if (!json_req) {
      ESP_LOGE("json", "Error before: [%s]", cJSON_GetErrorPtr());
      status = HTTPD_400;
      rsp_msg = "parse failed.";
      ret = web_srv_send_rsp(req, status, rsp_msg, strlen(rsp_msg));
      goto func_ret;
    }

    // Json Parse
    ret = web_srv_send_rsp(req, json_post_parser(json_req), NULL, 0);

func_ret:
    if (json_req)
        cJSON_Delete(json_req);
    if (buf)
        free(buf);
    return ret;
}

