#include <stdio.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <utils/wpa_debug.h>


/** DEFINES **/
#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1
#define MAX_FAILURES 10

static const char *TAG = "WIFI";

esp_err_t connect_wifi();
esp_err_t connect_tcp_server();
void app_main(void) // entry point for FreeRTOS
{
  esp_err_t status = WIFI_FAILURE;

  //Initialize storage to store wifi config
  esp_err_t ret = nvs_flash_init();

  //Check if there are remaining free pages in nvs, if not erase and reinit.
  if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret); // Check if no errors found

  //Connect to Wireless AP
  status = connect_wifi();
  if(WIFI_SUCCESS != status){
    ESP_LOGI(TAG, "Failed to connect to AP");
    return;
  }

  status = connect_tcp_server();
  if(TCP_SUCCESS != status){
    ESP_LOGI(TAG, "Failed to connect to remote server");
    return;
  }
}


esp_err_t connect_tcp_server() {
  return 0;
}

esp_err_t connect_wifi() {
  return 0;
}
