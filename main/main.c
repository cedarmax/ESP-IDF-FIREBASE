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

static const char *TAG = "WIFI";
static bool switch1_state = false;
static bool switch2_state = false;
#define USER_ID "SQSdfpXEHzSEW2WU7pkDBzWIA8i1"  // test user-id to be replaced with actual Firebase user ID later

// Function to retrieve and update switch states from Firestore
void fetch_firestore_data_task(void *pvParameter) {
    char response_buffer[512];

    while (1) {
        // Retrieve switch states from Firestore
        esp_err_t err = firebase_firestore_get_data("users/" USER_ID, response_buffer, sizeof(response_buffer));
        if (err == ESP_OK) {
            // Parse JSON response
            cJSON *root = cJSON_Parse(response_buffer);
            if (root != NULL) {
                cJSON *fields = cJSON_GetObjectItem(root, "fields");

                if (fields) {
                    cJSON *switch1 = cJSON_GetObjectItem(fields, "switch1");
                    cJSON *switch2 = cJSON_GetObjectItem(fields, "switch2");

                    if (switch1 && cJSON_IsBool(switch1)) {
                        switch1_state = cJSON_IsTrue(switch1);
                        ESP_LOGI("MAIN", "Switch 1: %s", switch1_state ? "ON" : "OFF");
                    }

                    if (switch2 && cJSON_IsBool(switch2)) {
                        switch2_state = cJSON_IsTrue(switch2);
                        ESP_LOGI("MAIN", "Switch 2: %s", switch2_state ? "ON" : "OFF");
                    }
                }

                cJSON_Delete(root);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));  // Fetch data every 5 seconds
    }
}

// Function to simulate switch toggling and update Firestore
void toggle_switch_task(void *pvParameter) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Simulate switch press every 10 seconds

        switch1_state = !switch1_state;
        switch2_state = !switch2_state;

        ESP_LOGI("MAIN", "Toggling Switch 1 to: %s", switch1_state ? "ON" : "OFF");
        update_switch_state(USER_ID, 1, switch1_state);

        ESP_LOGI("MAIN", "Toggling Switch 2 to: %s", switch2_state ? "ON" : "OFF");
        update_switch_state(USER_ID, 2, switch2_state);
    }
}

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

    // Test Firebase Realtime Database Upload
    const char *json = "{\"panelVoltage\": 95.5, \"batteryVoltage\": 70}";
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

     // Start Firestore sync tasks
     xTaskCreate(&fetch_firestore_data_task, "fetch_firestore_task", 4096, NULL, 5, NULL);
     xTaskCreate(&toggle_switch_task, "toggle_switch_task", 4096, NULL, 5, NULL);
}
