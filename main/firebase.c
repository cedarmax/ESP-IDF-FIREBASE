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
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

#define FTAG "REALTIME DATABASE"
#define RESPONSE_BUFFER_SIZE 4096
#define JTAG "JSON_PARSE"
#define TAG_SNTP "SNTP"

#define FIREBASE_PROJECT_ID "solar-control-app"
#define FIREBASE_API_KEY "AIzaSyC1i1-XbvHzPnnm1ZRSrZJpEJNuZM70F5U"
#define FIREBASE_DATABASE_URL "https://solar-control-app-default-rtdb.firebaseio.com/"

static const char *TAG = "WIFI";
static bool switch1_state = false;
static bool switch2_state = false;

//Function to retrieve the current user ID from Firestore
char USER_ID[64] = "";  // Global variable to hold current user ID

esp_err_t fetch_current_user_id() {
    char response_buffer[512];
    esp_err_t err = cloud_firestore_get_data("current_user/active", response_buffer, sizeof(response_buffer));
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_Parse(response_buffer);
    if (!root) return ESP_FAIL;

    cJSON *fields = cJSON_GetObjectItem(root, "fields");
    if (!fields) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *userIdObj = cJSON_GetObjectItem(fields, "userId");
    if (!userIdObj) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *userIdValue = cJSON_GetObjectItem(userIdObj, "stringValue");
    if (userIdValue && cJSON_IsString(userIdValue)) {
        strncpy(USER_ID, userIdValue->valuestring, sizeof(USER_ID) - 1);
        USER_ID[sizeof(USER_ID) - 1] = '\0';
        ESP_LOGI(TAG, "Retrieved current user ID: %s", USER_ID);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void monitor_user_id_task(void *pvParameter) {
    char last_user_id[128] = {0};

    while (1) {
        if (fetch_current_user_id() == ESP_OK) {
            if (strcmp(USER_ID, last_user_id) != 0) {
                ESP_LOGI(TAG, "Detected change in user ID: %s", USER_ID);
                strncpy(last_user_id, USER_ID, sizeof(last_user_id) - 1);
                last_user_id[sizeof(last_user_id) - 1] = '\0';
            }
        } else {
            ESP_LOGW(TAG, "Failed to fetch current user ID.");
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // check every 10 seconds
    }
}

// Function to retrieve and update switch states from Firestore
void fetch_firestore_data_task(void *pvParameter) {
    char response_buffer[1024];

    while (1) {
        // Retrieve switch states from Firestore
        char user_path[128];
        snprintf(user_path, sizeof(user_path), "users/%s", USER_ID);
        esp_err_t err = cloud_firestore_get_data(user_path, response_buffer, sizeof(response_buffer));
        if (err != ESP_OK) {
            ESP_LOGE("MAIN", "Failed to fetch Firestore data");
            vTaskDelay(pdMS_TO_TICKS(5000));  // Retry after 5 seconds
            continue;
        }
        
        // Parse JSON response
        cJSON *root = cJSON_Parse(response_buffer);
        if (!root) {
            ESP_LOGE("MAIN", "Failed to parse Firestore JSON");
            vTaskDelay(pdMS_TO_TICKS(5000));  // Retry after 5 seconds
            continue;
        }
        
        cJSON *fields = cJSON_GetObjectItem(root, "fields");
        if (!fields) {
            ESP_LOGE("MAIN", "No fields found in Firestore data");
            cJSON_Delete(root);
            vTaskDelay(pdMS_TO_TICKS(5000));  // Retry after 5 seconds
            continue;
        }

        // Update switch states safely
        cJSON *switch1 = cJSON_GetObjectItem(fields, "switch1");
        if (switch1 && cJSON_IsBool(switch1)) {
            switch1_state = cJSON_IsTrue(switch1);
            ESP_LOGI("MAIN", "Switch 1: %s", switch1_state ? "ON" : "OFF");
        }

        cJSON *switch2 = cJSON_GetObjectItem(fields, "switch2");
        if (switch2 && cJSON_IsBool(switch2)) {
            switch2_state = cJSON_IsTrue(switch2);
            ESP_LOGI("MAIN", "Switch 2: %s", switch2_state ? "ON" : "OFF");
        }

        cJSON_Delete(root);
        vTaskDelay(pdMS_TO_TICKS(5000));  // Fetch data every 5 seconds
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

void initialize_sntp() {
    ESP_LOGI(TAG_SNTP, "Initializing SNTP");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");  // Set NTP server
    esp_sntp_init();

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

    // --- Parse schedule map ---
    cJSON *schedule_obj = cJSON_GetObjectItem(fields, "schedule");
    if (schedule_obj) {
        cJSON *mapValue = cJSON_GetObjectItem(schedule_obj, "mapValue");
        if (mapValue) {
            cJSON *schedule_fields = cJSON_GetObjectItem(mapValue, "fields");
            if (schedule_fields) {
                cJSON *item = NULL;
                cJSON_ArrayForEach(item, schedule_fields) {
                    const char *key = item->string;
                    cJSON *valueObj = cJSON_GetObjectItem(item, "stringValue");
                    if (key && valueObj && cJSON_IsString(valueObj)) {
                        ESP_LOGI(JTAG, "Schedule - %s: %s", key, valueObj->valuestring);
                        // Here you can save or act on each time
                    }
                }
            }
        }
    }

    ESP_LOGI(JTAG, "Parsed Values - switch1: %s, switch2: %s",
             switch1 ? "true" : "false",
             switch2 ? "true" : "false");

    cJSON_Delete(root);
}

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
    if (USER_ID[0] == '\0') {
        ESP_LOGE(TAG, "USER_ID is empty. Cannot send data.");
        return ESP_ERR_INVALID_STATE;
    }
    
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

esp_err_t log_ina260_reading(const char *user_id, float current, float voltage, float power) {
    time_t now = time(NULL);

    char url[512];
    snprintf(url, sizeof(url), "%s/users/%s/ina260_log/%lld.json?auth=%s",
    FIREBASE_DATABASE_URL, user_id, (long long)now, FIREBASE_API_KEY);


    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "current", current);
    cJSON_AddNumberToObject(json, "voltage", voltage);
    cJSON_AddNumberToObject(json, "power", power);
    cJSON_AddNumberToObject(json, "timestamp", now);

    char *post_data = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI("INA260", "INA260 log sent: %s", post_data);
    } else {
        ESP_LOGE("INA260", "Failed to send INA260 log: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(post_data);
    return err;
}

// Function to parse timestamp string from Firestore
// This function assumes the timestamp is in the format "YYYY-MM-DDTHH:MM:SSZ"
time_t parse_timestamp(const char *timestamp_str) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    strptime(timestamp_str, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return mktime(&tm);
}

bool cloud_firestore_get_collection(const char* project_id, const char* collection, const char* api_key, void (*callback)(const cJSON*)) {
    if (!project_id || !collection || !api_key || !callback) {
        ESP_LOGE(TAG, "Invalid parameters for Firestore GET");
        return false;
    }

    char url[512];
    snprintf(url, sizeof(url),
             "https://firestore.googleapis.com/v1/projects/%s/databases/(default)/documents/%s?key=%s",
             project_id, collection, api_key);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "Firestore request failed with HTTP status %d", status_code);
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_get_content_length(client);
    char* response = malloc(content_length + 1);
    if (!response) {
        ESP_LOGE(TAG, "Memory allocation failed");
        esp_http_client_cleanup(client);
        return false;
    }

    int read_len = esp_http_client_read_response(client, response, content_length);
    response[read_len] = '\0';

    cJSON* json = cJSON_Parse(response);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        free(response);
        esp_http_client_cleanup(client);
        return false;
    }

    callback(json);

    cJSON_Delete(json);
    free(response);
    esp_http_client_cleanup(client);
    return true;
}



// Function to toggle switches based on schedule
// This function will run in a FreeRTOS task and check the schedule every minute
// It will toggle the switches based on the schedule retrieved from Firestore
#include "esp_log.h"

static const char *STAG = "SCHEDULE_TOGGLE";

void schedule_callback(const cJSON* root) {
    static bool toggled_today[24 * 60 * 2] = {0}; // extra space for multiple switches per minute

    if (!root || !cJSON_IsObject(root)) {
        ESP_LOGE(STAG, "Invalid JSON response.");
        return;
    }

    cJSON *documents = cJSON_GetObjectItem(root, "documents");
    if (!documents || !cJSON_IsArray(documents)) {
        ESP_LOGE(STAG, "No documents found in Firestore response.");
        return;
    }

    time_t now = time(NULL);
    struct tm current_tm;
    localtime_r(&now, &current_tm);

    for (int i = 0; i < cJSON_GetArraySize(documents); i++) {
        cJSON *doc = cJSON_GetArrayItem(documents, i);
        if (!doc) continue;

        cJSON *fields = cJSON_GetObjectItem(doc, "fields");
        if (!fields) continue;

        cJSON *switchItem = cJSON_GetObjectItem(fields, "switch");
        cJSON *turnOnItem = cJSON_GetObjectItem(fields, "turnOn");
        cJSON *timeItem = cJSON_GetObjectItem(fields, "time");
        if (!switchItem || !turnOnItem || !timeItem) continue;

        int switchIndex = cJSON_GetObjectItem(switchItem, "integerValue") ?
                          atoi(cJSON_GetObjectItem(switchItem, "integerValue")->valuestring) : 0;

        bool turnOn = cJSON_GetObjectItem(turnOnItem, "booleanValue") ?
                      cJSON_IsTrue(cJSON_GetObjectItem(turnOnItem, "booleanValue")) : false;

        time_t schedule_time = cJSON_GetObjectItem(timeItem, "timestampValue") ?
                               parse_timestamp(cJSON_GetObjectItem(timeItem, "timestampValue")->valuestring) : 0;

        if (schedule_time == 0) continue;

        struct tm schedule_tm;
        localtime_r(&schedule_time, &schedule_tm);

        if (schedule_tm.tm_year == current_tm.tm_year &&
            schedule_tm.tm_mon == current_tm.tm_mon &&
            schedule_tm.tm_mday == current_tm.tm_mday &&
            schedule_tm.tm_hour == current_tm.tm_hour &&
            schedule_tm.tm_min == current_tm.tm_min) {

            int toggle_key = switchIndex * 1440 + schedule_tm.tm_hour * 60 + schedule_tm.tm_min;

            if (!toggled_today[toggle_key]) {
                ESP_LOGI(STAG, "Scheduled match: switch %d at %02d:%02d, turning %s.",
                         switchIndex, schedule_tm.tm_hour, schedule_tm.tm_min, turnOn ? "ON" : "OFF");

                if (switchIndex == 1 && switch1_state != turnOn) {
                    switch1_state = turnOn;
                    update_switch_state(USER_ID, 1, switch1_state);
                } else if (switchIndex == 2 && switch2_state != turnOn) {
                    switch2_state = turnOn;
                    update_switch_state(USER_ID, 2, switch2_state);
                }

                toggled_today[toggle_key] = true;
            } else {
                ESP_LOGI(STAG, "Switch %d already toggled at %02d:%02d. Skipping.",
                         switchIndex, schedule_tm.tm_hour, schedule_tm.tm_min);
            }
        }
    }

    // Reset at midnight
    if (current_tm.tm_hour == 0 && current_tm.tm_min == 0) {
        ESP_LOGI(STAG, "Midnight reset: clearing toggle history.");
        memset(toggled_today, 0, sizeof(toggled_today));
    }
}

void scheduled_switch_toggle_task(void *pvParameter) {
    while (1) {
        if (USER_ID[0] == '\0') {
            ESP_LOGW(STAG, "USER_ID not set. Retrying in 10 seconds.");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        ESP_LOGI(STAG, "Fetching schedule for user: %s", USER_ID);

        char path[128];
        snprintf(path, sizeof(path), "users/%s/schedules", USER_ID);

        if (!cloud_firestore_get_collection(FIREBASE_PROJECT_ID, path, FIREBASE_API_KEY, schedule_callback)) {
            ESP_LOGE(STAG, "Failed to fetch schedules.");
        }

        vTaskDelay(pdMS_TO_TICKS(60000)); // wait a minute between checks
    }
}
