#include <stdio.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "firebase.h"

#define WIFI_SSID "Xcover"
#define WIFI_PASS "wifipass"

static const char *TAG = "ESP32C6_MAIN";

void wifi_init(void) {
    ESP_LOGI(TAG, "Initializing Wi-Fi...");
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi setup complete.");
}

void app_main(void) {
    nvs_flash_init();
    wifi_init();

    vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait for Wi-Fi to connect

    // Send data to Firebase
    send_firebase_rtdb_data();
    send_firebase_firestore_data();
    
    // Retrieve data from Firestore
    get_firebase_firestore_data();
}
