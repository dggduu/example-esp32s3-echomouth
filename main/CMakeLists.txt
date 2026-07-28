option(ENABLE_UNIT_TESTS "Build unit tests" OFF)

# 启用单元测试模式
set(ENABLE_UNIT_TESTS off)

set(main_srcs "")

if(ENABLE_UNIT_TESTS)
    list(APPEND main_srcs "unit_test_main.c")
else()
    list(APPEND main_srcs "sys_main.c")
endif()

list(APPEND main_srcs
    "sntp_helper.c"
    "cam_helper.c"
    "Pages/main_page.c"
    "Pages/chat_page.c"
    "Pages/menu_page.c"
    "Pages/cam_page.c"
    "Pages/page_todo.c"
    "Pages/page_ota.c"
    "Pages/debug_page.c"
    "Pages/cam_test_page.c"
    "utils/http_client_helper.c"
    "utils/s3_helper.c"
    "utils/ota_backend.c"
    "utils/nvs_helper.c"
    "utils/face_detector_helper.cc"
    "utils/mdns_helper"
    "utils/task_manager.c"
    "utils/monitor_mamager.c"
    "utils/aes_crypto_helper.c"
    "utils/img_queue.c"
    "utils/time_test_helper.c"
    "utils/esp_vfs_helper.c"
    "utils/efuse_helper.c"
    "utils/audio_helper.c"
    "utils/power_manager.c"
    "../esp_efuse_table_custom.c"
)

idf_component_register(
    SRCS ${main_srcs}
    INCLUDE_DIRS "." "utils" "Pages" "../include"
)

if(ENABLE_UNIT_TESTS)
    target_link_options(${COMPONENT_LIB} PRIVATE -Wl,--no-gc-sections)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE UNIT_TEST_MODE=1)
endif()

if(NOT ENABLE_UNIT_TESTS)
    create_esp_enc_img(${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
        ${project_dir}/assets/private.pem ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_secure.bin app)

    littlefs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)


# ULP support additions to component CMakeLists.txt.
#
# 1. The ULP app name must be unique (if multiple components use ULP).
set(ulp_app_name ulp_${COMPONENT_NAME})
#
# 2. Specify all C and Assembly source files.
#    Files should be placed into a separate directory (in this case, ulp/),
#    which should not be added to COMPONENT_SRCS.
set(ulp_riscv_sources "ulp/ulp_main.c")

#
# 3. List all the component source files which include automatically
#    generated ULP export file, ${ulp_app_name}.h:
set(ulp_exp_dep_srcs "sys_main.c" "utils/power_manager.c")

#
# 4. Call function to build ULP binary and embed in project using the argument
#    values above.
ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")