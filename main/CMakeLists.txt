idf_component_register(SRCS 
                        "main.c"
                         
                        "esp32_s3_szp.c" 
                        "sntp_helper.c" 
                        "cam_helper.c"
                        "Pages/main_page.c"
                        "Pages/chat_page.c"
                        "Pages/menu_page.c"
                        "Pages/cam_page.c"
                        "Pages/page_todo.c"
                        "Pages/page_ota.c"

                        "utils/http_client_helper.c"
                        "utils/s3_helper.c"
                        # "utils/img_stack.c"
                        "utils/ota_backend.c"
                        "utils/nvs_helper.c"
                        "utils/face_detector_helper.cc"
                        "utils/task_manager.c"
                        "utils/monitor_mamager.c"
                        "utils/img_queue.c"
                        "utils/time_test_helper.c"

                        INCLUDE_DIRS "." "utils" "pages" 
                        # EMBED_TXTFILES ${project_dir}/assets/private.pem
                    )

create_esp_enc_img(${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    ${project_dir}/assets/private.pem ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_secure.bin app)

littlefs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)