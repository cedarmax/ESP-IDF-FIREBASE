#include "firebase.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"

#define TAG "FIREBASE"

// Firebase HTTP POST (Send Data)
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
        ESP_LOGI(TAG, "Data Sent! HTTP Status: %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "HTTP Request Failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Firebase HTTP GET (Retrieve Data)
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
