// main/main.c (Modified from the original code)

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h" // Include the WiFi headers
#include "wifi_connect.h"  //Use an external or your own wifi_connect component
#include "app_firebase.h" // Include Firebase-related code


// WiFi Configuration (Replace with your actual credentials)
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

static const char *TAG = "main";

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

   //Connect to wifi
   wifi_connect(WIFI_SSID, WIFI_PASSWORD);

    //Initialize and start Firebase
    app_firebase_init();
    
    //Example use of app_firebase_upload_data
	cJSON *data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "message", "Hello from ESP32");
    cJSON_AddNumberToObject(data, "value", 42);
	app_firebase_upload_data("/test", data);
	cJSON_Delete(data);
}

