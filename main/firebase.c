#include "firebase.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"

#define TAG "FIREBASE"

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
esp_err_t firebase_firestore_get_data(const char *path, char *response_buffer, size_t buffer_size) {
    char url[256];
    snprintf(url, sizeof(url), "https://firestore.googleapis.com/v1/projects/solar-control-app/databases/(default)/documents/%s?key=%s",
             path, FIREBASE_API_KEY);

    ESP_LOGI(TAG, "Firestore GET URL: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 20000,  // Increase timeout
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");  // Request fixed-length response

    esp_err_t err = ESP_FAIL;
    int retry_count = 3;

    while (retry_count-- > 0) {
        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "Firestore HTTP Status Code: %d", status_code);

            if (status_code == 200) {
                int content_length = esp_http_client_get_content_length(client);
                ESP_LOGI(TAG, "Content Length: %d", content_length);

                if (content_length == -1) {
                    // Handle chunked transfer encoding
                    ESP_LOGW(TAG, "Content-Length is -1. Reading response as chunked transfer encoding.");
                    int total_read = 0, read_len;
                    do {
                        read_len = esp_http_client_read(client, response_buffer + total_read, buffer_size - 1 - total_read);
                        if (read_len > 0) {
                            total_read += read_len;
                            ESP_LOGI(TAG, "Read %d bytes, Total Read: %d", read_len, total_read);
                        } else if (read_len == 0) {
                            ESP_LOGW(TAG, "End of response reached.");
                            break;
                        } else {
                            ESP_LOGE(TAG, "Error reading chunked response: %s", esp_err_to_name(read_len));
                            err = ESP_FAIL;
                            break;
                        }
                    } while (read_len > 0 && total_read < buffer_size - 1);

                    if (total_read > 0) {
                        response_buffer[total_read] = '\0';  // Null-terminate
                        ESP_LOGI(TAG, "Received from Firestore (chunked): %s", response_buffer);

                        // Parse JSON response
                        cJSON *json = cJSON_Parse(response_buffer);
                        if (json == NULL) {
                            ESP_LOGE(TAG, "Failed to parse JSON response");
                            err = ESP_FAIL;
                        } else {
                            // Process JSON data
                            cJSON_Delete(json);
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed to read response from Firestore (total_read = %d)", total_read);
                        err = ESP_FAIL;
                    }
                } else if (content_length > 0) {
                    // Handle fixed-length response
                    int total_read = esp_http_client_read(client, response_buffer, buffer_size - 1);
                    if (total_read > 0) {
                        response_buffer[total_read] = '\0';  // Null-terminate
                        ESP_LOGI(TAG, "Received from Firestore: %s", response_buffer);

                        // Parse JSON response
                        cJSON *json = cJSON_Parse(response_buffer);
                        if (json == NULL) {
                            ESP_LOGE(TAG, "Failed to parse JSON response");
                            err = ESP_FAIL;
                        } else {
                            // Process JSON data
                            cJSON_Delete(json);
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed to read response from Firestore");
                        err = ESP_FAIL;
                    }
                } else {
                    ESP_LOGE(TAG, "Empty or invalid response from Firestore");
                    err = ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "Firestore returned HTTP status %d", status_code);
                if (status_code == 401) {
                    ESP_LOGE(TAG, "Authentication failed. Check API key.");
                } else if (status_code == 404) {
                    ESP_LOGE(TAG, "Document not found. Check the path.");
                }
                err = ESP_FAIL;
            }
            break;  // Exit loop on success or valid response
        } else {
            ESP_LOGW(TAG, "Retrying Firestore GET (%d retries left)...", retry_count);
            vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second before retrying
        }
    }

    esp_http_client_cleanup(client);
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

