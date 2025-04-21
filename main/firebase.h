#ifndef FIREBASE_H
#define FIREBASE_H

#include <stdio.h>
#include "esp_http_client.h"
#include "esp_err.h"
#include <stdbool.h>

#define FIREBASE_PROJECT_ID "solar-control-app"
#define FIREBASE_API_KEY "AIzaSyC1i1-XbvHzPnnm1ZRSrZJpEJNuZM70F5U"
#define FIREBASE_DATABASE_URL "https://solar-control-app-default-rtdb.firebaseio.com/"

//wifi


// Firebase Prototypes
esp_err_t firebase_send_data(float battery_voltage);
esp_err_t cloud_firestore_get_data(const char *path, char *response_buffer, size_t buffer_size);
esp_err_t update_switch_state(const char *user_id, int switch_number, bool state);
esp_err_t log_ina260_reading(const char *user_id, float current, float voltage, float power);
esp_err_t fetch_current_user_id(void);

esp_err_t cloud_firestore_get_collection(const char *collection, const char *document, const char *subcollection, char *response_buffer, size_t buffer_size);


// Tasks
void fetch_firestore_data_task(void *pvParameter);
//void toggle_switch_task(void *pvParameter);
void upload_battery_task(void *param);  // 
void scheduled_switch_toggle_task(void *pvParameter);


// WiFi event handler
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

// SNTP (NTP time sync)
void initialize_sntp(void);

#endif // FIREBASE_H
