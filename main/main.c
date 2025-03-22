#include <stdio.h>
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "firebase.h"
#include "esp_log.h"
#include "esp_netif.h"

static const char *TAG = "WIFI";

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connected to WiFi!");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGE(TAG, "Disconnected! Retrying...");
        esp_wifi_connect();
    }
}

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

    // Test Firebase Upload
    const char *json = "{\"panelVoltage\": 25.5, \"batteryVoltage\": 60}";
    if (firebase_send_data("sensors/data", json) == ESP_OK) {
        ESP_LOGI(TAG, "Data uploaded to Firebase successfully!");
    } else {
        ESP_LOGE(TAG, "Failed to upload data to Firebase!");
    }
    
    // Test Firebase Get Data
    char buffer[512];
    if (firebase_get_data("sensors/data", buffer, sizeof(buffer)) == ESP_OK) {
        ESP_LOGI(TAG, "Received data from Firebase: %s", buffer);
    } else {
        ESP_LOGE(TAG, "Failed to get data from Firebase!");
    }
}
