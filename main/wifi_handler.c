#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "lwip/ip6.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi_handler.h"
#include "configuration_adapter.h"

#define TAG "wifi softAP"
#define STA_TAG "Wifi STA"
#define AP_TAG "Wifi AP"
static EventGroupHandle_t s_connect_event_group;
static ip4_addr_t s_ipv4_addr;
static ip6_addr_t s_ipv6_addr;
static wifi_cfg_t s_wifi_cfg = {0};

void wifi_hdl_start_service()
{
  s_connect_event_group = xEventGroupCreate();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &on_ap_assign_sta_ip, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

  if (wifi_sta_preferred()) {
    // STA mode
    wifi_init_sta();
  } else {
    // AP mode only
    wifi_init_softap();
  }
  ESP_ERROR_CHECK(esp_wifi_start());

  xTaskCreate(wifi_user_task, "wifi_user_task", 2048, NULL, 2, NULL);
}

esp_err_t wifi_hdl_sta_query_ap(char* ssid, size_t ssid_len) {
    esp_err_t ret = ESP_OK;
    wifi_ap_record_t ap_info;

    ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) {
        return ret;
    }

    strncpy(ssid, (char*)ap_info.ssid, ssid_len);
    ssid[ssid_len - 1] = '\0';

    return ESP_OK;
}

esp_err_t wifi_hdl_query_ip_info(tcpip_adapter_if_t sta_0_ap_1, ip_info_t* ip_info) {
    if (sta_0_ap_1 > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    struct netif * netif = NULL;
    if (tcpip_adapter_get_netif(sta_0_ap_1, (void**) &netif) == ESP_OK) {
        snprintf(ip_info->ip4_addr, IPV4_ADDR_MAXLEN, IPSTR, IP2STR(&(netif->ip_addr.u_addr.ip4)));
        snprintf(ip_info->ip4_netmask, IPV4_ADDR_MAXLEN, IPSTR, IP2STR(&(netif->netmask.u_addr.ip4)));
        snprintf(ip_info->ip4_gateway, IPV4_ADDR_MAXLEN, IPSTR, IP2STR(&(netif->gw.u_addr.ip4)));

        ip_info->ip6_count = 0;
        for (size_t i=0; i<LWIP_IPV6_NUM_ADDRESSES; i++) {
            if (ip6_addr_isvalid(netif->ip6_addr_state[i])) {
                snprintf(ip_info->ip6_addr[ip_info->ip6_count++], IPV6_ADDR_MAXLEN,
                         IPV6STR, IPV62STR(netif->ip6_addr[i].u_addr.ip6));
            }
        }

        return ESP_OK;
    }

    return ESP_FAIL;
}

uint8_t wifi_hdl_sta_connect(char ssid[WIFI_SSID_MAXLEN], char password[WIFI_PASS_MAXLEN]) {
    if (s_wifi_cfg.sta_conn_status == STA_CONNECTING) {
        return 0;
    }

    xSemaphoreTake(s_wifi_cfg.sta_mutex_req, portMAX_DELAY);
    if (ssid != NULL && strlen(ssid) > 0) {
        strcpy(s_wifi_cfg.sta_ssid_req, ssid);
        if (password != NULL && strlen(password) > 0) {
            strcpy(s_wifi_cfg.sta_pass_req, password);
        } else {
            s_wifi_cfg.sta_pass_req[0] = '\0';
        }
    } else {
        s_wifi_cfg.sta_ssid_req[0] = '\0';
    }
    xSemaphoreGive(s_wifi_cfg.sta_mutex_req);

    xEventGroupSetBits(s_connect_event_group, WIFI_EGBIT_STA_CONN_REQ);
    ESP_LOGI(STA_TAG, "wifi_hdl_sta_connect()");

    return 1;
}

uint8_t wifi_hdl_sta_query_status() {
    return (uint8_t) s_wifi_cfg.sta_conn_status;
}

void wifi_hdl_sta_disconnect() {
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_LOGI(STA_TAG, "wifi_hdl_sta_disconnect()");
}

uint8_t  wifi_hdl_ap_turn_on() {
    wifi_init_softap();
    ESP_LOGI(STA_TAG, "wifi_hdl_ap_turn_on()");
    return 1;
}

uint8_t wifi_hdl_ap_turn_off() {
    wifi_mode_t wifi_mode;
    wifi_ap_record_t ap_rec;

    ESP_LOGI(STA_TAG, "wifi_hdl_ap_turn_off()");

    ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode));
    if (wifi_mode == WIFI_MODE_APSTA && esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        return 1;
    }

    return 0;
}

esp_err_t wifi_hdl_ap_query(char* ssid, size_t ssid_len) {
    wifi_mode_t wifi_mode;
    wifi_config_t wifi_cfg;

    ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode));
    if (wifi_mode != WIFI_MODE_AP && wifi_mode != WIFI_MODE_APSTA) {
        return ESP_ERR_INVALID_STATE;
    }

    if (esp_wifi_get_config(ESP_IF_WIFI_AP, &wifi_cfg) == ESP_OK) {
        strncpy(ssid, (char*)wifi_cfg.ap.ssid, ssid_len);
        ssid[ssid_len - 1] = '\0';
    }

    return ESP_OK;
}

void wifi_ap_gen_ssid(char* ssid)
{
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
    snprintf(ssid, WIFI_SSID_MAXLEN, "%s %02X%02X%02X%02X%02X%02X", CFG_WIFI_AP_SSID_DEFAULT,
                                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void wifi_ap_load_cfg(wifi_config_t* wifi_config) {
    uint8_t auth = WIFI_AUTH_MAX;
    size_t ssid_len = WIFI_SSID_MAXLEN;
    size_t pass_len = WIFI_PASS_MAXLEN;
    ESP_ERROR_CHECK(cfg_adp_get_by_id(CFG_WIFI_SSID_AP, wifi_config->ap.ssid, &ssid_len));
    ESP_ERROR_CHECK(cfg_adp_get_by_id(CFG_WIFI_PASS_AP, wifi_config->ap.password, &pass_len));
    ESP_ERROR_CHECK(cfg_adp_get_u8_by_id(CFG_WIFI_AUTH_AP, &auth));
    ESP_ERROR_CHECK(cfg_adp_get_u8_by_id(CFG_WIFI_MAX_CONN_AP, &wifi_config->ap.max_connection));
    if (strlen((char*) wifi_config->ap.ssid) == 0) {
        wifi_ap_gen_ssid((char*) wifi_config->ap.ssid);
    }
    wifi_config->ap.ssid_len = strlen((char*)wifi_config->ap.ssid);
    if (strlen((char*) wifi_config->ap.password) == 0) {
        auth = WIFI_AUTH_OPEN;
    }
    if (0 == wifi_config->ap.max_connection)
    {
      // at lease 1 connection.
      wifi_config->ap.max_connection = 1;
    }
    wifi_config->ap.authmode = auth;
}

void wifi_sta_load_cfg(wifi_config_t* wifi_config) {
    size_t ssid_len = WIFI_SSID_MAXLEN;
    size_t pass_len = WIFI_PASS_MAXLEN;
    ESP_ERROR_CHECK(cfg_adp_get_by_id(CFG_WIFI_SSID, (char*)wifi_config->sta.ssid, &ssid_len));
    ESP_ERROR_CHECK(cfg_adp_get_by_id(CFG_WIFI_PASS, (char*)wifi_config->sta.password, &pass_len));
}

uint8_t wifi_sta_retry_before_ap() {
    uint8_t ret;
    ESP_ERROR_CHECK(cfg_adp_get_u8_by_id(CFG_WIFI_STA_MAX_RETRY, &ret));
    return ret;
}

uint8_t wifi_sta_preferred() {
    uint8_t ret;
    ESP_ERROR_CHECK(cfg_adp_get_u8_by_id(CFG_WIFI_MODE, &ret));
    return ret;
}

void wifi_check_sta() {
    wifi_mode_t wifi_mode;
    wifi_sta_list_t sta_list;
    wifi_ap_record_t ap_rec;
    wifi_config_t wifi_config = {0};

    // Turn off the AP if STA mode is preferred and no STA connects to the AP.
    if (wifi_sta_preferred()) {
        ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode));

        if (wifi_mode == WIFI_MODE_APSTA) {
            ESP_ERROR_CHECK(esp_wifi_ap_get_sta_list(&sta_list));

            if (sta_list.num == 0) {
                ESP_LOGI(AP_TAG, "Switch-off the backup AP. Enter STA-only mode.");
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

                if (esp_wifi_sta_get_ap_info(&ap_rec) != ESP_OK) {
                    wifi_sta_load_cfg(&wifi_config);
                    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
                    ESP_ERROR_CHECK(esp_wifi_connect());
                    ESP_LOGI(STA_TAG, "Reconnecting to %s", (char*) wifi_config.sta.ssid);
                }
            }
        }
    }
}

void on_got_ip(void *arg, esp_event_base_t event_base,
               int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    memcpy(&s_ipv4_addr, &event->ip_info.ip, sizeof(s_ipv4_addr));
    xEventGroupSetBits(s_connect_event_group, WIFI_EGBIT_GOT_IPV4);
}

void on_got_ipv6(void *arg, esp_event_base_t event_base,
                 int32_t event_id, void *event_data) {
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    memcpy(&s_ipv6_addr, &event->ip6_info.ip, sizeof(s_ipv6_addr));
    xEventGroupSetBits(s_connect_event_group, WIFI_EGBIT_GOT_IPV6);
}

void on_ap_assign_sta_ip(void *arg, esp_event_base_t event_base,
                         int32_t event_id, void *event_data) {
    s_wifi_cfg.apsta_stop_scan_flag = 1;
}

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_check_sta();
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_START) {
        // IPv6 - FE80::1
        ESP_ERROR_CHECK(tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_AP));
        struct netif * netif = NULL;
        ESP_ERROR_CHECK(tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_AP, (void**) &netif));
        ip6_addr_t ip6ll;
        ESP_ERROR_CHECK(ip6addr_aton("FE80::1", &ip6ll) == 1 ? 0 : 1);
        netif_add_ip6_address(netif, &ip6ll, NULL);

        // IPv4 - 10.1.10.1
        tcpip_adapter_ip_info_t info = { 0, };
		IP4_ADDR(&info.ip, 10, 1, 10, 1);
		IP4_ADDR(&info.gw, 10, 1, 10, 1);
		IP4_ADDR(&info.netmask, 255, 255, 255, 0);
		ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
		ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
		ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));

		memcpy(&s_ipv4_addr, &info.ip, sizeof(s_ipv4_addr));
	    xEventGroupSetBits(s_connect_event_group, WIFI_EGBIT_GOT_IPV4);
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        // TODO: initialize ipv6 as configuration
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
        xEventGroupSetBits(s_connect_event_group, WIFI_EGBIT_STA_CONN);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("STA_DISC", "%d", ((wifi_event_sta_disconnected_t*)event_data)->reason);
        xEventGroupSetBits(s_connect_event_group, WIFI_EGBIT_STA_DISC);
    } else if (event_id == WIFI_EVENT_STA_START) {
      // esp_wifi_start() was called.
        if (wifi_sta_preferred()) {
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
    }
}

void wifi_init_softap() {
    wifi_config_t wifi_config = {0};
    wifi_ap_load_cfg(&wifi_config);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
             (char*) wifi_config.ap.ssid, (char*) wifi_config.ap.password);
}

void wifi_init_sta() {
    wifi_config_t wifi_config = { 0 };
    wifi_sta_load_cfg(&wifi_config);
    if (wifi_config.sta.ssid[0] == '\0') {
        ESP_LOGW(TAG, "Empty SSID in STA mode, switching to AP mode.");
        wifi_init_softap();
        return;
    }

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
}

void wifi_cfg_sta() {
    wifi_config_t wifi_config = { 0 };

    xSemaphoreTake(s_wifi_cfg.sta_mutex_req, portMAX_DELAY);
    if (s_wifi_cfg.sta_ssid_req[0] != '\0') {
        strcpy((char*) wifi_config.sta.ssid, s_wifi_cfg.sta_ssid_req);
        wifi_config.sta.ssid[WIFI_SSID_MAXLEN - 1] = '\0';
        if (s_wifi_cfg.sta_pass_req[0] != '\0') {
            strcpy((char*) wifi_config.sta.password, s_wifi_cfg.sta_pass_req);
            wifi_config.sta.password[WIFI_PASS_MAXLEN - 1] = '\0';
        } else {
            wifi_config.sta.password[0] = '\0';
        }

        s_wifi_cfg.sta_ssid_req[0] = '\0';
        s_wifi_cfg.sta_pass_req[0] = '\0';
    }
    xSemaphoreGive(s_wifi_cfg.sta_mutex_req);
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
}

void timer_sta_reconn_expire(xTimerHandle pxTimer) {
    xEventGroupSetBits(s_connect_event_group, WIFI_EGBIT_STA_RECONN);
}

void wifi_user_task(void* param) {
    TimerHandle_t timer_sta_reconn = xTimerCreate("sta_reconn", 200, pdFALSE, NULL, timer_sta_reconn_expire);
    wifi_mode_t wifi_mode;

    s_wifi_cfg.sta_mutex_req = xSemaphoreCreateMutex();
    s_wifi_cfg.sta_conn_status = STA_DISC;

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_connect_event_group, WIFI_EGBIT_ALL, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & WIFI_EGBIT_GOT_IPV4) {
            ESP_LOGI(TAG, "IPv4 address: " IPSTR, IP2STR(&s_ipv4_addr));
        }

        if (bits & WIFI_EGBIT_GOT_IPV6) {
            ESP_LOGI(TAG, "IPv6 address: " IPV6STR, IPV62STR(s_ipv6_addr));
        }

        if (bits & WIFI_EGBIT_STA_DISC) {
            s_wifi_cfg.sta_conn_status = STA_DISC;

            ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode));
            if (wifi_sta_preferred() && (wifi_mode == WIFI_MODE_STA || (wifi_mode == WIFI_MODE_APSTA && !s_wifi_cfg.apsta_stop_scan_flag))) {
                xTimerStart(timer_sta_reconn, portMAX_DELAY);
            }

            ESP_LOGW(STA_TAG, "Lost connection to AP.");
        }

        if (bits & WIFI_EGBIT_STA_RECONN) {
            ESP_ERROR_CHECK(esp_wifi_get_mode(&wifi_mode));

            if (wifi_mode == WIFI_MODE_STA || (wifi_mode == WIFI_MODE_APSTA && !s_wifi_cfg.apsta_stop_scan_flag)) {
                ESP_LOGI(STA_TAG, "[%d] Attempt to re-establish connection to AP...", s_wifi_cfg.sta_retry_num);
                ESP_ERROR_CHECK(esp_wifi_connect());
            }

            if (s_wifi_cfg.apsta_stop_scan_flag) {
                s_wifi_cfg.sta_retry_num = 0;
                s_wifi_cfg.apsta_stop_scan_flag = 0;
                ESP_LOGI(AP_TAG, "AP got client, STA stops further reconnection retry.");
            }

            if (s_wifi_cfg.sta_retry_num >= wifi_sta_retry_before_ap()) {
                // Switch to AP_STA mode and continue attempting...

                if (wifi_mode == WIFI_MODE_STA) {
                    ESP_LOGI(AP_TAG, "Backup AP has been turned on to prevent lost of access.");
                    wifi_init_softap();
                }
            } else {
                s_wifi_cfg.sta_retry_num++;
            }
        }

        if (bits & WIFI_EGBIT_STA_CONN) {
            ESP_LOGI(STA_TAG, "Connection established.");
            s_wifi_cfg.sta_retry_num = 0;
            s_wifi_cfg.sta_conn_status = STA_CONNECTED;

            wifi_check_sta();
        }

        if (bits & WIFI_EGBIT_STA_CONN_REQ) {
            if (s_wifi_cfg.sta_conn_status != STA_CONNECTING) {
                s_wifi_cfg.sta_conn_status = STA_CONNECTING;

                wifi_cfg_sta();
                ESP_ERROR_CHECK(esp_wifi_disconnect());
                ESP_ERROR_CHECK(esp_wifi_connect());
            }
        }

    }
}
