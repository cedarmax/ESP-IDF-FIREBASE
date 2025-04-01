#include "firebase.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "esp_err.h"
#include <stdlib.h>
#include <string.h>

#define TAG "FIREBASE"
#define RESPONSE_BUFFER_SIZE 4096

// Realtime Database HTTP POST (Send Data)
esp_err_t firebase_send_data(const char *path, const char *json) {
    char url[256];
    snprintf(url, sizeof(url), "%s/%s.json?auth=%s", FIREBASE_DATABASE_URL, path, FIREBASE_API_KEY);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,  // Use PUT to replace, POST to append
        .timeout_ms = 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Use built-in certificate bundle
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Realtime Database Data Sent! HTTP Status: %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "Realtime Database HTTP Request Failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Realtime Database HTTP GET (Retrieve Data)
esp_err_t firebase_get_data(const char *path, char *response_buffer, size_t buffer_size) {
    char url[256];
    snprintf(url, sizeof(url), "%s/%s.json?auth=%s", FIREBASE_DATABASE_URL, path, FIREBASE_API_KEY);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,  // Ensure SSL verification
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int content_length = esp_http_client_get_content_length(client);
        esp_http_client_read(client, response_buffer, buffer_size);
        response_buffer[content_length] = '\0';
        ESP_LOGI(TAG, "Received: %s", response_buffer);
    } else {
        ESP_LOGE(TAG, "HTTP Request Failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Cloud Firestore HTTP GET (Retrieve Data)
typedef struct {
    char *buffer;
    int buffer_len;
} client_data_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    client_data_t *client_data = evt->user_data;
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int prev_len = client_data->buffer_len;
                char *new_buffer = realloc(client_data->buffer, prev_len + evt->data_len + 1);
                if (new_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory in event handler");
                    return ESP_FAIL;
                }
                client_data->buffer = new_buffer;
                memcpy(client_data->buffer + prev_len, evt->data, evt->data_len);
                client_data->buffer_len += evt->data_len;
                client_data->buffer[client_data->buffer_len] = 0;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t firebase_firestore_get_data(const char *path, char *response_buffer, size_t buffer_size) {
    char url[256];
    snprintf(url, sizeof(url), "https://firestore.googleapis.com/v1/projects/solar-control-app/databases/(default)/documents/%s?key=%s",
             path, FIREBASE_API_KEY);

    ESP_LOGI(TAG, "Firestore GET URL: %s", url);

    // Initialize structure to store response
    client_data_t client_data;
    client_data.buffer = malloc(1);
    client_data.buffer_len = 0;
    if (client_data.buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate initial memory for response");
        return ESP_ERR_NO_MEM;
    }
    client_data.buffer[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = _http_event_handler,
        .user_data = &client_data
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Firestore HTTP Status Code: %d", status_code);

        if (status_code == 200 && client_data.buffer_len > 0) {
            strncpy(response_buffer, client_data.buffer, buffer_size - 1);
            response_buffer[buffer_size - 1] = '\0';
            ESP_LOGI(TAG, "Received Firestore Response: %s", response_buffer);
        } else {
            ESP_LOGE(TAG, "Failed to read response from Firestore (total_read = %d)", client_data.buffer_len);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Firestore GET Failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(client_data.buffer);
    return err;
}


// Cloud Firestore HTTP PATCH (Update Data)
esp_err_t update_switch_state(const char *user_id, int switch_number, bool state) {
    char url[256];
    snprintf(url, sizeof(url), "https://firestore.googleapis.com/v1/projects/solar-control-app/databases/(default)/documents/users/%s?updateMask.fieldPaths=switch%d&key=%s",
             user_id, switch_number, FIREBASE_API_KEY);

    char json_body[128];
    snprintf(json_body, sizeof(json_body),
             "{\"fields\": {\"switch%d\": {\"booleanValue\": %s}}}",
             switch_number, state ? "true" : "false");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PATCH,
        .timeout_ms = 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Firestore Switch %d Updated: %s", switch_number, json_body);
    } else {
        ESP_LOGE(TAG, "Firestore PATCH Failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

