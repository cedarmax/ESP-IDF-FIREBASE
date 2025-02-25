// app_firebase.h
#ifndef _APP_FIREBASE_H_
#define _APP_FIREBASE_H_

#include "cJSON.h"  // Make sure cJSON is included
#include "esp_err.h"

esp_err_t app_firebase_init(void);
esp_err_t app_firebase_upload_data(const char *path, cJSON *data);
void app_firebase_deinit(void);

#endif
