#include <stdlib.h>
#include <string.h>

#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "configuration_adapter.h"

static config_def_t config_defs[CFG_IDT_MAX] = {
    [CFG_WIFI_SSID] =           {.name = "wifi_sta_ssid",       .type = CFG_DATA_STR,   .default_val.str = NULL,    .validate.str = NULL},
    [CFG_WIFI_PASS] =           {.name = "wifi_sta_pass",       .type = CFG_DATA_STR,   .default_val.str = NULL,    .validate.str = NULL},
    [CFG_WIFI_STA_MAX_RETRY] =  {.name = "wifi_sta_retry",      .type = CFG_DATA_U8,    .default_val.u8 = 5,        .validate.u8 = NULL},
    [CFG_WIFI_SSID_AP] =        {.name = "wifi_ap_ssid",    .type = CFG_DATA_STR,   .default_val.str = NULL,    .validate.str = NULL},
    [CFG_WIFI_PASS_AP] =        {.name = "wifi_ap_pass",    .type = CFG_DATA_STR,   .default_val.str = CFG_WIFI_AP_PASS_DEFAULT,    .validate.str = NULL},
    [CFG_WIFI_AUTH_AP] =        {.name = "wifi_ap_auth",    .type = CFG_DATA_U8,    .default_val.u8 = 4,        .validate.u8 = cfg_adp_check_ap_auth},
    [CFG_WIFI_MAX_CONN_AP] =    {.name = "wifi_ap_conn",    .type = CFG_DATA_U8,    .default_val.u8 = CFG_WIFI_AP_MAX_CONN_DEFAULT, .validate.u8 = NULL},
    [CFG_WIFI_MODE] =           {.name = "wifi_mode",       .type = CFG_DATA_U8,    .default_val.u8 = 1,        .validate.u8 = NULL},

    [CFG_UART_BAUD] =           {.name = "uart_baud_rate",  .type = CFG_DATA_U32,   .default_val.u32 = 9600,    .validate.u32 = cfg_adp_check_set_baudrate},
    [CFG_UART_PARITY] =         {.name = "uart_parity",     .type = CFG_DATA_U8,    .default_val.u8 = 0,        .validate.u8 = cfg_adp_check_set_parity},
    [CFG_UART_TX_DELAY] =       {.name = "uart_tx_delay",   .type = CFG_DATA_U32,   .default_val.u32 = 1,       .validate.u32 = cfg_adp_check_set_tx_delay},

    [CFG_SW_1] =                {.name = "switch1",   .type = CFG_DATA_U8,   .default_val.u8 = 0,       .validate.u8 = NULL},
    [CFG_SW_2] =                {.name = "switch2",   .type = CFG_DATA_U8,   .default_val.u8 = 0,       .validate.u8 = NULL},
    [CFG_SW_3] =                {.name = "switch3",   .type = CFG_DATA_U8,   .default_val.u8 = 0,       .validate.u8 = NULL},

};

enum cfg_data_idt cfg_adp_id_from_name(const char* name) {
    for (enum cfg_data_idt i = 0; i<CFG_IDT_MAX; i++) {
        if (strcmp(name, config_defs[i].name) == 0) {
            return i;
        }
    }

    return CFG_IDT_MAX;
}

char* cfg_adp_name_from_id(enum cfg_data_idt id) {
    return cfg_adp_is_valid_id(id) ? config_defs[id].name : NULL;
}

esp_err_t cfg_adp_get_by_id(enum cfg_data_idt id, void* buf, size_t* maxlen) {
    if (!cfg_adp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    esp_err_t err = ESP_OK;
    nvs_handle cfg_nvss_handle;
    size_t param_len;

    // Open
    err = nvs_open(CFG_STORAGE_NAMESPACE, NVS_READWRITE, &cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

    switch (cfg->type) {
    case CFG_DATA_STR:
        param_len = *maxlen;
        err = nvs_get_str(cfg_nvss_handle, cfg->name, (char*)buf, &param_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (cfg->default_val.str != NULL) {
                strncpy(buf, cfg->default_val.str, *maxlen);
            } else {
                *((char*)buf) = '\0';
            }
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
        *maxlen = param_len;
        break;

    case CFG_DATA_U8:
        err = nvs_get_u8(cfg_nvss_handle, cfg->name, (uint8_t*) buf);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *((uint8_t*)buf) = cfg->default_val.u8;
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
        break;

    case CFG_DATA_U32:
        err = nvs_get_u32(cfg_nvss_handle, cfg->name, (uint32_t*) buf);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *((uint32_t*)buf) = cfg->default_val.u32;
            err = ESP_OK;
        } else if (err != ESP_OK) {
            goto err_ret;
        }
        break;

    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

err_ret:
    // Close
    nvs_close(cfg_nvss_handle);
    return err;
}

esp_err_t cfg_adp_set_by_id(enum cfg_data_idt id, const void* buf) {
    if (!cfg_adp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    nvs_handle cfg_nvss_handle;
    esp_err_t err = ESP_OK;

    // Open
    err = nvs_open(CFG_STORAGE_NAMESPACE, NVS_READWRITE, &cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

    switch (cfg->type) {
    case CFG_DATA_STR:
        if (cfg->validate.str != NULL)
            err = cfg->validate.str((const char*)buf);
        if (err == ESP_OK)
            err = nvs_set_str(cfg_nvss_handle, cfg->name, (const char*)buf);
        break;
    case CFG_DATA_U8:
        if (cfg->validate.u8 != NULL)
            err = cfg->validate.u8((uint8_t) ((uint32_t)buf));
        if (err == ESP_OK)
            err = nvs_set_u8(cfg_nvss_handle, cfg->name, (uint8_t) ((uint32_t)buf));
        break;
    case CFG_DATA_U32:
        if (cfg->validate.u32 != NULL)
            err = cfg->validate.u32((uint32_t)buf);
        if (err == ESP_OK)
            err = nvs_set_u32(cfg_nvss_handle, cfg->name, (uint32_t)buf);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    if (err != ESP_OK)
        goto err_ret;

    // Commit written value.
    // After setting any values, nvs_commit() must be called to ensure changes are written
    // to flash storage. Implementations may write to storage at other times,
    // but this is not guaranteed.
    err = nvs_commit(cfg_nvss_handle);
    if (err != ESP_OK)
        goto err_ret;

err_ret:
    // Close
    nvs_close(cfg_nvss_handle);
    return err;
}

esp_err_t cfg_adp_set_by_id_from_raw(enum cfg_data_idt id, const char* param) {
    if (!cfg_adp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    esp_err_t err = ESP_OK;
    uint32_t u32_var;

    switch (cfg->type) {
    case CFG_DATA_STR:
        err = cfg_adp_set_by_id(id, param);
        break;
    case CFG_DATA_U8:
    case CFG_DATA_U32:
        u32_var = atoi(param);
        err = cfg_adp_set_by_id(id, (void*) u32_var);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }

    return err;
}

esp_err_t cfg_adp_get_by_id_to_readable(enum cfg_data_idt id, char* buf, size_t maxlen) {
    if (!cfg_adp_is_valid_id(id))
        return ESP_ERR_NOT_SUPPORTED;

    config_def_t* cfg = &(config_defs[id]);
    esp_err_t err = ESP_OK;
    uint32_t u32_var = 0;

    switch (cfg->type) {
    case CFG_DATA_STR:
        err = cfg_adp_get_by_id(id, buf, &maxlen);
        break;
    case CFG_DATA_U8:
    case CFG_DATA_U32:
        err = cfg_adp_get_by_id(id, (void*) &u32_var, NULL);
        snprintf(buf, maxlen, "%d", u32_var);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    return err;
}

esp_err_t cfg_adp_check_set_baudrate(uint32_t baudrate) {
    if (baudrate >= 1200 && baudrate <= 921600) {
        // TODO: set modbus tcp slave baudrate
        //modbus_uart_set_baudrate(baudrate);
        return ESP_OK;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t cfg_adp_check_set_parity(uint8_t parity) {
    if (parity < 3) {
        // TODO: set modbus tcp slave parity
        //modbus_uart_set_parity(parity);
        return ESP_OK;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t cfg_adp_check_set_tx_delay(uint32_t tx_delay) {
    if (tx_delay <= 1024) {
        // TODO: set modbus tcp slave parity
        //modbus_uart_set_tx_delay(tx_delay);
        return ESP_OK;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t cfg_adp_check_ap_auth(uint8_t auth) {
    return (auth < WIFI_AUTH_MAX) ? ESP_OK : ESP_ERR_INVALID_ARG;
}
