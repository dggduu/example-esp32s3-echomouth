#include "esp_vfs_helper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "unity.h"
#include <string.h>
#include <unistd.h>


static const char *TEST_MULTI = "/storage/multi.txt";

static void test_cleanup_files(void) { unlink(TEST_MULTI); }

static SemaphoreHandle_t s_done = NULL;
static volatile int s_failed = 0;

static void writer_task(void *arg) {
  int id = (int)arg;
  char buf[32];
  for (int i = 0; i < 20; i++) {
    sprintf(buf, "Task%d:%d\n", id, i);
    if (vfs_helper_write_file_from_ram(TEST_MULTI, (uint8_t *)buf,
                                       strlen(buf)) != ESP_OK) {
      s_failed = 1;
    }
    vTaskDelay(1);
  }
  xSemaphoreGive(s_done);
  vTaskDelete(NULL);
}

static void reader_task(void *arg) {
  (void)arg;
  for (int i = 0; i < 20; i++) {
    uint8_t *buf = NULL;
    size_t len;
    vfs_helper_read_file_to_ram(TEST_MULTI, &buf, &len);
    if (buf)
      free(buf);
    vTaskDelay(2);
  }
  xSemaphoreGive(s_done);
  vTaskDelete(NULL);
}

TEST_CASE("VFS: concurrent writes", "[vfs_multi]") {
  test_cleanup_files();

  s_failed = 0;
  s_done = xSemaphoreCreateCounting(3, 0);
  xTaskCreate(writer_task, "w1", 4096, (void *)1, 5, NULL);
  xTaskCreate(writer_task, "w2", 4096, (void *)2, 5, NULL);
  xTaskCreate(writer_task, "w3", 4096, (void *)3, 5, NULL);
  for (int i = 0; i < 3; i++)
    xSemaphoreTake(s_done, pdMS_TO_TICKS(10000));
  TEST_ASSERT_EQUAL(0, s_failed);
  vSemaphoreDelete(s_done);

  test_cleanup_files();
}

TEST_CASE("VFS: concurrent reads", "[vfs_multi]") {
  test_cleanup_files();

  // 预先创建测试文件
  const char *content = "test data for reads";
  vfs_helper_write_file_from_ram(TEST_MULTI, (uint8_t *)content,
                                 strlen(content));

  s_done = xSemaphoreCreateCounting(3, 0);
  xTaskCreate(reader_task, "r1", 4096, NULL, 5, NULL);
  xTaskCreate(reader_task, "r2", 4096, NULL, 5, NULL);
  xTaskCreate(reader_task, "r3", 4096, NULL, 5, NULL);
  for (int i = 0; i < 3; i++)
    xSemaphoreTake(s_done, pdMS_TO_TICKS(10000));
  vSemaphoreDelete(s_done);

  test_cleanup_files();
}