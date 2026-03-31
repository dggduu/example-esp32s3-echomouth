idf_component_register(SRCS "esp32_s3_szp.c" "main.c" "sntp_helper.c"
                    INCLUDE_DIRS "."
                    )

littlefs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
