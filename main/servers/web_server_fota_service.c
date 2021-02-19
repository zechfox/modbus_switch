#include <sys/param.h>

#include <esp_ota_ops.h>
#include <esp_log.h>
#include "sdkconfig.h"

#include "web_server.h"
#include "web_server_fota_service.h"


#define OTA_BUF_SIZE   256 
#define otaTag "webServer FOTA"

esp_err_t web_srv_fota_service(httpd_req_t *req) {
  esp_ota_handle_t update_handle = 0;
  const esp_partition_t *update_partition = NULL;
  char* resp_str = "Upgrading OK.";
  char* status = NULL;
  ESP_LOGI(otaTag, "Starting OTA...");
  update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    status = HTTPD_500;
    resp_str = "Passive OTA partition not found";
    ESP_LOGE(otaTag, resp_str);
    return web_srv_send_rsp(req, status, resp_str, strlen(resp_str));
  }
  ESP_LOGI(otaTag, "Writing to partition subtype %d at offset 0x%x",
           update_partition->subtype, update_partition->address);

  esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
  if (err != ESP_OK) {
    resp_str = "OTA Begin Failed.";
    status = HTTPD_500;
    ESP_LOGE(otaTag, "%s, error=%d", resp_str, err);
    return web_srv_send_rsp(req, status, resp_str, strlen(resp_str));
  }
  ESP_LOGI(otaTag, "esp_ota_begin succeeded");
  ESP_LOGI(otaTag, "Please Wait. This may take time");

  size_t remaining = req->content_len;
  ESP_LOGI(otaTag, "Total Upgrade data size: %d bytes", remaining);

  esp_err_t ota_write_err = ESP_OK;
  char *upgrade_data_buf = (char *)malloc(OTA_BUF_SIZE);
  if (!upgrade_data_buf) {
    status = HTTPD_500;
    resp_str = "Couldn't allocate memory to upgrade data buffer";
    ESP_LOGE(otaTag, resp_str);
    return web_srv_send_rsp(req, status, resp_str, strlen(resp_str));
  }
  int binary_file_len = 0;
  while (1) {
    int data_read = httpd_req_recv(req, upgrade_data_buf, OTA_BUF_SIZE);
    if (data_read == 0) {
      ESP_LOGI(otaTag, "Connection closed,all data received");
      break;
    }
    if (data_read < 0) {
      ESP_LOGE(otaTag, "Error: SSL data read error");
      err = ESP_FAIL;
      break; 
    }
    if (data_read > 0) {
//      ESP_LOG_BUFFER_HEX(otaTag, upgrade_data_buf, data_read);
      ota_write_err = esp_ota_write( update_handle, (const void *)upgrade_data_buf, data_read);
      if (ota_write_err != ESP_OK) {
        break;
      }
      binary_file_len += data_read;
      ESP_LOGI(otaTag, "Written image length %d", binary_file_len);
    }
  }
  free(upgrade_data_buf);
  ESP_LOGD(otaTag, "Total binary data length writen: %d", binary_file_len);

  esp_err_t ota_end_err = esp_ota_end(update_handle);
  if (ESP_OK != err)
  {
    return err;
  }
  if (ota_write_err != ESP_OK) {
    ESP_LOGE(otaTag, "Error: esp_ota_write failed! err=0x%d", err);
    status = HTTPD_500;
    resp_str = "OTA Write Failed";
    return web_srv_send_rsp(req, status, resp_str, strlen(resp_str));
  } else if (ota_end_err != ESP_OK) {
    ESP_LOGE(otaTag, "Error: esp_ota_end failed! err=0x%d. Image is invalid", ota_end_err);
    return ota_end_err;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    status = HTTPD_500;
    resp_str = "ESP Set Boot Partition Failed";
    ESP_LOGE(otaTag, "esp_ota_set_boot_partition failed! err=0x%d", err);
    return web_srv_send_rsp(req, status, resp_str, strlen(resp_str));
  }
  ESP_LOGI(otaTag, "esp_ota_set_boot_partition succeeded"); 

  xTaskCreate(restart_task, "restart_task", 1024, NULL, 6, NULL);
  return web_srv_send_rsp(req, NULL, resp_str, strlen(resp_str));

}
