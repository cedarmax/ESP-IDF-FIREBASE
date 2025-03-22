#ifndef FIREBASE_H
#define FIREBASE_H

#include <stdio.h>
#include "esp_http_client.h"

#define FIREBASE_PROJECT_ID "solar-control-app"
#define FIREBASE_API_KEY "AIzaSyC1i1-XbvHzPnnm1ZRSrZJpEJNuZM70F5U"
#define FIREBASE_DATABASE_URL "https://solar-control-app-default-rtdb.firebaseio.com/"

// Function Prototypes
esp_err_t firebase_send_data(const char *path, const char *json);
esp_err_t firebase_get_data(const char *path, char *response_buffer, size_t buffer_size);

#endif // FIREBASE_H
