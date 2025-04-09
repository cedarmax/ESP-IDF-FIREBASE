#include "firebase.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "esp_err.h"
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h" // Include for timestamp
#include <stdio.h>
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FTAG "REALTIME DATABASE"
#define RESPONSE_BUFFER_SIZE 4096
#define JTAG "JSON_PARSE"

#include "esp_sntp.h"

#define TAG_SNTP "SNTP"

//from main:

static const char *TAG = "WIFI";
static bool switch1_state = false;
static bool switch2_state = false;
#define USER_ID "SQSdfpXEHzSEW2WU7pkDBzWIA8i1" 

// Function to retrieve and update switch states from Firestore
void fetch_firestore_data_task(void *pvParameter) {
    char response_buffer[512];

    while (1) {
        // Retrieve switch states from Firestore
        esp_err_t err = cloud_firestore_get_data("users/" USER_ID, response_buffer, sizeof(response_buffer));
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

//end from main

void initialize_sntp() {
    ESP_LOGI(TAG_SNTP, "Initializing SNTP");

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");  // Set NTP server
    sntp_init();

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int max_retries = 10;
    
    while (timeinfo.tm_year < (2025 - 1900) && ++retry < max_retries) {
        ESP_LOGI(TAG_SNTP, "Waiting for system time to be set... (%d/%d)", retry, max_retries);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    
    if (timeinfo.tm_year < (2025 - 1900)) {
        ESP_LOGE(TAG_SNTP, "Failed to get valid time from NTP!");
    } else {
        ESP_LOGI(TAG_SNTP, "SNTP Time Synchronized: %s", asctime(&timeinfo));
    }
}


void parse_firestore_response(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(JTAG, "Failed to parse JSON");
        return;
    }

    cJSON *fields = cJSON_GetObjectItem(root, "fields");
    if (!fields) {
        ESP_LOGE(JTAG, "No 'fields' object found in JSON");
        cJSON_Delete(root);
        return;
    }

    // Extract switch1
    cJSON *switch1_obj = cJSON_GetObjectItem(fields, "switch1");
    bool switch1 = switch1_obj ? cJSON_IsTrue(cJSON_GetObjectItem(switch1_obj, "booleanValue")) : false;

    // Extract switch2
    cJSON *switch2_obj = cJSON_GetObjectItem(fields, "switch2");
    bool switch2 = switch2_obj ? cJSON_IsTrue(cJSON_GetObjectItem(switch2_obj, "booleanValue")) : false;

    ESP_LOGI(JTAG, "Parsed Values - switch1: %s, switch2: %s",
             switch1 ? "true" : "false",
             switch2 ? "true" : "false");

    cJSON_Delete(root);
}

#include <time.h>
#include <sys/time.h>

int64_t get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);  // Fetch real-time clock
    return tv.tv_sec;         // Return Unix time in seconds
}

char* get_iso_timestamp() {
    static char timestamp[20];
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    localtime_r(&now, &timeinfo);

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return timestamp;
}

// Realtime Database HTTP POST (Send Data)
esp_err_t firebase_send_data(float batteryVoltage) {
    char url[512];
    char json[128];

    // Get the current timestamp
    int64_t timestamp = get_timestamp();
    snprintf(json, sizeof(json), "{\"%lld\": {\"batteryVoltage\": %.2f}}", timestamp, batteryVoltage);

    snprintf(url, sizeof(url), "%s/.json?auth=%s", FIREBASE_DATABASE_URL, FIREBASE_API_KEY);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PATCH,  // Append data without replacing everything
        .timeout_ms = 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI("FIREBASE", "Data Sent: %s", json);
    } else {
        ESP_LOGE("FIREBASE", "HTTP Request Failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

//test function to continuously send graph data to realtime database
void upload_battery_task(void *param) {
    // Seed random number generator once
    srand((unsigned int)time(NULL));

    while (1) {
        // Random float between 12.0 and 14.0
        float voltage = 12.0f + ((float)rand() / RAND_MAX) * 2.0f;

        if (firebase_send_data(voltage) == ESP_OK) {
            ESP_LOGI("Realtime Database", "Uploaded voltage: %.2f", voltage);
        } else {
            ESP_LOGE("Realtime Database", "Upload failed!");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds
    }
}

// Cloud Firestore HTTP GET (Retrieve Data)
esp_err_t client_event_get_handler(esp_http_client_event_handle_t evt)
{
    static char *response_buffer = NULL;
    static int response_len = 0;

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            char *new_buffer = realloc(response_buffer, response_len + evt->data_len + 1);
            if (new_buffer == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for response");
                return ESP_FAIL;
            }
            response_buffer = new_buffer;
            memcpy(response_buffer + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            response_buffer[response_len] = '\0';
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "Received Firestore Response: %s", response_buffer);
        free(response_buffer);
        response_buffer = NULL;
        response_len = 0;
        break;
    default:
        break;
    }
    return ESP_OK;
}
typedef struct {
    char *buffer;
    int buffer_len;
} client_data_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    client_data_t *client_data = evt->user_data;
    //ESP_LOGE(TAG, "http event handled trash");
    //ESP_LOGE(TAG, "HTTP Request Failed evt->event_id: %d",evt->event_id);
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (true) {
                int prev_len = client_data->buffer_len;
                char *new_buffer = realloc(client_data->buffer, prev_len + evt->data_len + 1);
                if (new_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory in event handler");
                    return ESP_FAIL;
                }
                client_data->buffer = new_buffer;
                memcpy(client_data->buffer + prev_len, evt->data, evt->data_len);
                //ESP_LOGE(TAG, "buffer len incremented");
                client_data->buffer_len += evt->data_len;
                client_data->buffer[client_data->buffer_len] = 0;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t cloud_firestore_get_data(const char *path, char *response_buffer, size_t buffer_size) {
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

            // Parse the Firestore JSON response
            parse_firestore_response(response_buffer);

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

