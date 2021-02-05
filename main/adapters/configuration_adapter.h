#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

#define CFG_WIFI_AP_SSID_DEFAULT "Modbus Switch"
#define CFG_WIFI_AP_PASS_DEFAULT "password"
#define CFG_WIFI_AP_MAX_CONN_DEFAULT 3
#define CFG_STORAGE_NAMESPACE "app_cfg"

enum cfg_data_type {
    CFG_DATA_UNKNOWN = 0,
    CFG_DATA_STR = 1,
    CFG_DATA_U8 = 2,
    CFG_DATA_U32 = 3
};

enum cfg_data_idt {
    CFG_WIFI_SSID,
    CFG_WIFI_PASS,
    CFG_WIFI_STA_MAX_RETRY,
    CFG_WIFI_SSID_AP,
    CFG_WIFI_PASS_AP,
    CFG_WIFI_AUTH_AP,
    CFG_WIFI_MAX_CONN_AP,
    CFG_WIFI_MODE,

    CFG_UART_BAUD,
    CFG_UART_PARITY,
    CFG_UART_TX_DELAY,

    CFG_SW_1,
    CFG_SW_2,
    CFG_SW_3,
    CFG_IDT_MAX
};

typedef esp_err_t (*validater_str_t)(const char*);
typedef esp_err_t (*validater_u8_t)(uint8_t);
typedef esp_err_t (*validater_u32_t)(uint32_t);

typedef struct config_def {
    char* name;
    enum cfg_data_type type;
    union {
        char* str;
        uint8_t u8;
        uint32_t u32;
    } default_val;
    union {
        validater_str_t str;
        validater_u8_t u8;
        validater_u32_t u32;
    } validate;
} config_def_t;

#define cfg_adp_is_valid_id(id) (id >= 0 && id < CFG_IDT_MAX)
enum cfg_data_idt cfg_adp_id_from_name(const char* name);
char* cfg_adp_name_from_id(enum cfg_data_idt id);
esp_err_t cfg_adp_get_by_id(enum cfg_data_idt id, void* buf, size_t* maxlen);
esp_err_t cfg_adp_set_by_id(enum cfg_data_idt id, const void* buf);
esp_err_t cfg_adp_set_by_id_from_raw(enum cfg_data_idt id, const char* param);
esp_err_t cfg_adp_get_by_id_to_readable(enum cfg_data_idt id, char* buf, size_t maxlen);
#define cfg_adp_get_u8_by_id(id, out_addr) (cfg_adp_get_by_id(id, (void*)(out_addr), NULL))
#define cfg_adp_set_u8_by_id(id, out_addr) (cfg_adp_set_by_id(id, (void*)((uint8_t)out_addr)))
#define cfg_adp_get_u32_by_id(id, out_addr) (cfg_adp_get_by_id(id, (void*)(out_addr), NULL))
#define cfg_adp_set_u32_by_id(id, out_addr) (cfg_adp_set_by_id(id, (void*)(out_addr)))

esp_err_t cfg_adp_check_set_baudrate(uint32_t baudrate);
esp_err_t cfg_adp_check_set_parity(uint8_t parity);
esp_err_t cfg_adp_check_set_tx_delay(uint32_t tx_delay);
esp_err_t cfg_adp_check_ap_auth(uint8_t auth);

