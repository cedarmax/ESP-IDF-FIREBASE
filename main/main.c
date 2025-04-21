#include <stdio.h>
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "firebase.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Xcover",
            .password = "wifipass",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Wait for connection
    vTaskDelay(pdMS_TO_TICKS(5000));

    fetch_current_user_id();

    //Realtime Database upload test

    initialize_sntp();  // Ensure accurate time before Firebase uploads
    vTaskDelay(2000 / portTICK_PERIOD_MS);  // Small delay for time sync

    xTaskCreate(&upload_battery_task, "upload_battery_task", 4096, NULL, 5, NULL);

     // Start Firestore sync tasks
     xTaskCreate(&fetch_firestore_data_task, "fetch_firestore_task", 4096, NULL, 5, NULL);
     //xTaskCreate(&toggle_switch_task, "toggle_switch_task", 4096, NULL, 5, NULL);

     //schedule task
     xTaskCreate(&scheduled_switch_toggle_task, "scheduled_switch_toggle_task", 32768, NULL, 5, NULL);


     // voltage: 0%: 11.6% , 12.9 = 100%
}
