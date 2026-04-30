idf_component_register(
    SRCS
        ${CMAKE_CURRENT_LIST_DIR}/wifi_prov.c
        ${CMAKE_CURRENT_LIST_DIR}/prov_sec2_gen.c
        ${CMAKE_CURRENT_LIST_DIR}/prov_qr.c
        # ${CMAKE_CURRENT_LIST_DIR}/socket_helper.c
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES esp_wifi nvs_flash wifi_provisioning
    REQUIRES
        UI
        mbedtls
        efuse
        log
        json
        lvgl
        esp_websocket_client main
)