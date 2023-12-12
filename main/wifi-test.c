#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"

/** DEFINES **/
#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1
#define MAX_FAILURES 10

/** Globals **/
// Task Tag
static const char *TAG = "WIFI";

// Event group to contain status information
static EventGroupHandle_t wifi_event_group;

// Retry tracker
static int s_retry_num = 0;


// Event handler for wi-fi events
static void wifi_event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data
){
  if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
    ESP_LOGI(TAG, "Connecting to AP...");
    esp_wifi_connect(); //If successful, DHCP IP lease will allow to continue to the ip_event_handler
  }
  else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
    if(s_retry_num < MAX_FAILURES){
      ESP_LOGI(TAG, "Reconnecting to AP... Retry Attempt: %i", s_retry_num);
      esp_wifi_connect();
      s_retry_num ++;
    }
    else{
      xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
    }
  }
}

static void ip_event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data
){
  if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "STA IP: "IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
  }
}


// Connect to the server and return the result
esp_err_t connect_tcp_server(void) { // POSIX compliant
  struct sockaddr_in serverInfo = {0};
  char readBuffer[2048] = {0};

  serverInfo.sin_family = AF_INET;
  serverInfo.sin_addr.s_addr = 0x3009A8C0; // hardcoded ip
  serverInfo.sin_port = htons(12345); // hardcoded port


  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock < 0){
    ESP_LOGE(TAG, "Failed to create a socket ...");
    return TCP_FAILURE;
  }

  if(connect(sock, (struct sockaddr*)&serverInfo, sizeof(serverInfo)) != 0){
    ESP_LOGE(TAG, "Failed to connect to %s!", inet_ntoa(serverInfo.sin_addr.s_addr));
    close(sock);
    return TCP_FAILURE;
  }

  ESP_LOGI(TAG, "Connected to TCP server");
  static bool flag = true;

  while(flag){
    bzero(readBuffer, sizeof(readBuffer));
    int r = read(sock, readBuffer, sizeof(readBuffer)-1);
    for(int i = 0; i<r; i++){
      //putchar(readBuffer[i]);
    }

    ESP_LOGI(TAG, "Received: %s", readBuffer);

    if(strcmp(readBuffer, "HELLO") == 0){
      ESP_LOGI(TAG, "Success Received HELLO!!!!!");
      flag = false;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // delay to avoid thrashing CPU
  }
  return TCP_SUCCESS;
}


// Connect to Wi-Fi and return result
esp_err_t connect_wifi() {
  int status = WIFI_FAILURE;

  /**Initialize all network related tasks **/
  // Initialize the esp network interface driver
  ESP_ERROR_CHECK(esp_netif_init()); // Check return value to see if return is 0 else throw abort and why

  // Initialize default esp event loop
  // esp to be run using event driven programming;
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create Wi-Fi station in the Wi-Fi driver
  esp_netif_create_default_wifi_sta();

  // Setup Wi-Fi station with default configuration
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));


  /** EVENT LOOP **/

  // Output of event status is stored here
  wifi_event_group = xEventGroupCreate();

  esp_event_handler_instance_t wifi_handler_event_instance;
  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
          WIFI_EVENT,
          ESP_EVENT_ANY_ID, //Any Wi-Fi event
          &wifi_event_handler, // Call this function
          NULL,
          &wifi_handler_event_instance // Will be used to close event handler and remove from event group
          )
      );

  esp_event_handler_instance_t got_ip_event_instance;
  ESP_ERROR_CHECK(
      esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &ip_event_handler,
            NULL,
            &got_ip_event_instance
          )
      );


  /** START THE WIFI DRIVER **/
  wifi_config_t wifi_config = {
      .sta = {
          .ssid = "",
          .password = "",
          .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
          .pmf_cfg = {
              .capable = true,
              .required = false
          },
      },
  };


  // Set the Wi-Fi controller to be a station
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));


  // Set the Wi-Fi config
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  // Start the Wi-Fi driver
  ESP_ERROR_CHECK(esp_wifi_start()); // event loop for wi-fi controller starts

  ESP_LOGI(TAG, "STA Initialization Complete");

  /** WAIT **/
  // The below blocks until WIFI_SUCCESS | WIFI_FAILURE is returned from wifi_event_group
  EventBits_t bits = xEventGroupWaitBits(
      wifi_event_group,
      WIFI_SUCCESS | WIFI_FAILURE,
      pdFALSE,
      pdFALSE,
      portMAX_DELAY // 1 sec
      );

  // xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event happened
  if(bits & WIFI_SUCCESS){
    ESP_LOGI(TAG, "Connected to AP");
    status = WIFI_SUCCESS;
  }
  else if(bits & WIFI_FAILURE){
    ESP_LOGI(TAG, "Failed to connect to AP");
    status = WIFI_FAILURE;
  }
  else{
    ESP_LOGE(TAG, "Unexpected Event!!!");
    status = WIFI_FAILURE;
  }


  /* The event will not be processed after unregister */
  ESP_ERROR_CHECK(
      esp_event_handler_instance_unregister(
          IP_EVENT,
          IP_EVENT_STA_GOT_IP,
          got_ip_event_instance
      )
  );

  ESP_ERROR_CHECK(
      esp_event_handler_instance_unregister(
          WIFI_EVENT,
          ESP_EVENT_ANY_ID,
          wifi_handler_event_instance
      )
  );

  vEventGroupDelete(wifi_event_group);

  return status;
}


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
    ESP_LOGI(TAG, "Failed to connect to AP, dying...");
    return;
  }

  status = connect_tcp_server();
  if(TCP_SUCCESS != status){
    ESP_LOGI(TAG, "Failed to connect to remote server, dying...");
    return;
  }
}
