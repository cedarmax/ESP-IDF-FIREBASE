cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "main.c" "firebase.c"  # Add all your source files here
    INCLUDE_DIRS "."
    REQUIRES esp-tls esp_wifi esp_event nvs_flash esp_http_client json esp_timer
)
