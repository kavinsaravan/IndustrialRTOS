idf_component_register(SRCS "telemetry.cpp" "network.cpp" INCLUDE_DIRS "include"
    REQUIRES control diagnostics esp_wifi esp_event esp_netif nvs_flash mqtt)
