cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "main.c"  # Replace with "main.cpp" if using C++
    INCLUDE_DIRS "."
    REQUIRES esp_http_client nvs_flash
)