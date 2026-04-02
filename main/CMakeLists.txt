idf_component_register(SRCS "esp32_s3_szp.c" "main.c" "sntp_helper.c" "cam_helper.c"
                        "Pages/main_page.c"
                        "Pages/chat_page.c"
                        "Pages/menu_page.c"
                        "Pages/cam_page.c"
                        INCLUDE_DIRS "." 
                    )

littlefs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
