#pragma once
#include <stdio.h>
#include "esp_netif.h"
#include "freertos/event_groups.h"

#define WIFI_EGBIT_GOT_IPV4     BIT(0)
#define WIFI_EGBIT_GOT_IPV6     BIT(1)
#define WIFI_EGBIT_STA_DISC     BIT(2)
#define WIFI_EGBIT_STA_RECONN   BIT(3)
#define WIFI_EGBIT_STA_CONN     BIT(4)
#define WIFI_EGBIT_AP_STA_LEAVE BIT(5)
#define WIFI_EGBIT_CFG_CHANGED  BIT(6)
#define WIFI_EGBIT_STA_CONN_REQ BIT(7)
#define WIFI_EGBIT_ALL (0xFF)
#define IPV4_ADDR_MAXLEN 16
#define IPV6_ADDR_MAXLEN 40
#define IPV6_ADDR_COUNT 3
#define WIFI_SSID_MAXLEN 32
#define WIFI_PASS_MAXLEN 64




enum sta_conn_status {
    STA_DISC,
    STA_CONNECTING,
    STA_CONNECTED
};

typedef struct wifi_cfg{
    // State
    uint8_t sta_retry_num;
    uint8_t apsta_stop_scan_flag;
    enum sta_conn_status sta_conn_status;

    // STA connect request
    SemaphoreHandle_t  sta_mutex_req;
    char sta_ssid_req[WIFI_SSID_MAXLEN];
    char sta_pass_req[WIFI_PASS_MAXLEN];
} wifi_cfg_t; 

typedef struct ip_info {
    char ip4_addr[IPV4_ADDR_MAXLEN];
    char ip4_netmask[IPV4_ADDR_MAXLEN];
    char ip4_gateway[IPV4_ADDR_MAXLEN];
    size_t ip6_count;
    char ip6_addr[IPV6_ADDR_COUNT][IPV6_ADDR_MAXLEN];
} ip_info_t;

// handler API
void wifi_hdl_start_service();
esp_err_t wifi_hdl_sta_query_ap(char* ssid, size_t ssid_len);
esp_err_t wifi_hdl_query_ip_info(tcpip_adapter_if_t sta_0_ap_1, ip_info_t* ip_info);
uint8_t wifi_hdl_sta_connect(char ssid[WIFI_SSID_MAXLEN], char password[WIFI_PASS_MAXLEN]);
uint8_t wifi_hdl_sta_query_status();
void wifi_hdl_sta_disconnect();
uint8_t  wifi_hdl_ap_turn_on();
uint8_t wifi_hdl_ap_turn_off();
esp_err_t wifi_hdl_ap_query(char* ssid, size_t ssid_len);

// internal function
void wifi_ap_gen_ssid(char* ssid);
void wifi_ap_load_cfg(wifi_config_t* wifi_config);
void wifi_sta_load_cfg(wifi_config_t* wifi_config);
uint8_t wifi_sta_retry_before_ap();
uint8_t wifi_sta_preferred();
void wifi_check_sta();
void on_got_ip(void *arg, esp_event_base_t event_base,
               int32_t event_id, void *event_data);
void on_got_ipv6(void *arg, esp_event_base_t event_base,
                 int32_t event_id, void *event_data);
void on_ap_assign_sta_ip(void *arg, esp_event_base_t event_base,
                         int32_t event_id, void *event_data);
void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data);
void wifi_init_softap();
void wifi_init_sta();
void wifi_cfg_sta();
void timer_sta_reconn_expire(xTimerHandle pxTimer);
void wifi_user_task(void* param);

