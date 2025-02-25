// app_firebase.c
#include "app_firebase.h"
#include "esp_log.h"
#include "firebase_config.h"
#include "firebase.h"

static const char *TAG = "app_firebase";
static firebase_t *fb;

esp_err_t app_firebase_init(void)
{
    // Initialize Firebase.  This often involves setting up config and auth.
	firebase_config_init(&config); //Sets up Kconfig, see sdkconfig.defaults
    
    fb = firebase_init(&config);

    if (!fb) {
        ESP_LOGE(TAG, "Failed to initialize Firebase");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Firebase Initialized");
    return ESP_OK;
}
//Firebase deinitialization
void app_firebase_deinit(void){
	firebase_deinit(fb);
}

esp_err_t app_firebase_upload_data(const char *path, cJSON *data)
{   
    char *json_string = cJSON_Print(data);
    if (!json_string) {
       ESP_LOGE(TAG, "Failed to serialize JSON data");
       return ESP_FAIL;
    }
      // Upload the data
    if (firebase_set_string(fb, path, json_string) == FIREBASE_STATUS_OK) {
        ESP_LOGI(TAG, "Data uploaded successfully to %s", path);
		free(json_string);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to upload data to %s", path);
		free(json_string);
        return ESP_FAIL;
    }  
}
