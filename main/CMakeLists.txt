// main/CMakeLists.txt
idf_component_register(SRCS "main.c" "wifi_connect.c" "app_firebase.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash log freertos esp_event esp_netif esp_timer json )

target_link_libraries(${COMPONENT_LIB} "-L${CMAKE_CURRENT_LIST_DIR}/../../../components/cloud/firebase/lib/esp32" "-lfirebase")
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../components/cloud/firebase/include)

