#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"

#define FIREBASE_RTDB_URL "https://your-project-id.firebaseio.com/test.json"
#define FIREBASE_FIRESTORE_URL "https://firestore.googleapis.com/v1/projects/your-project-id/databases/(default)/documents/testCollection"
#define FIREBASE_AUTH "your_firebase_auth_token"

static const char *TAG = "FIREBASE";

void send_http_request(const char *url, const char *data, const char *method) {
    esp_http_client_config_t config = {
        .url = url,
        .method = strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_PATCH,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", "Bearer " FIREBASE_AUTH);
    esp_http_client_set_post_field(client, data, strlen(data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Response: %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "HTTP Request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void send_firebase_rtdb_data() {
    char data[100];
    int random_value = esp_random() % 100;
    snprintf(data, sizeof(data), "{\"random_value\": %d}", random_value);

    ESP_LOGI(TAG, "Sending data to Firebase RTDB...");
    send_http_request(FIREBASE_RTDB_URL, data, "PATCH");
}

void send_firebase_firestore_data() {
    char data[200];
    int random_value = esp_random() % 100;
    snprintf(data, sizeof(data), "{\"fields\": {\"random_value\": {\"integerValue\": \"%d\"}}}", random_value);

    ESP_LOGI(TAG, "Sending data to Firestore...");
    send_http_request(FIREBASE_FIRESTORE_URL, data, "POST");
}

void get_firebase_firestore_data() {
    esp_http_client_config_t config = {
        .url = FIREBASE_FIRESTORE_URL,
        .method = HTTP_METHOD_GET,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer " FIREBASE_AUTH);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Received Firestore Data: %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "Failed to get Firestore Data: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
