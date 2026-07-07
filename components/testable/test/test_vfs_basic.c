#include "esp_vfs_helper.h"
#include "unity.h"
#include <string.h>
#include <unistd.h>

static const char *TEST_FILE1 = "/storage/test1.txt";
static const char *TEST_FILE2 = "/storage/test2.txt";
static const char *TEST_COPY = "/storage/test_copy.txt";

static void test_cleanup_files(void) {
  unlink(TEST_FILE1);
  unlink(TEST_FILE2);
  unlink(TEST_COPY);
}

TEST_CASE("VFS: write and read file", "[vfs_basic]") {
  test_cleanup_files();

  const uint8_t data[] = "Hello VFS!";
  size_t len = sizeof(data) - 1;
  TEST_ASSERT_EQUAL(ESP_OK,
                    vfs_helper_write_file_from_ram(TEST_FILE1, data, len));

  uint8_t *buf = NULL;
  size_t read_len;
  TEST_ASSERT_EQUAL(ESP_OK,
                    vfs_helper_read_file_to_ram(TEST_FILE1, &buf, &read_len));
  TEST_ASSERT_EQUAL(len, read_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data, buf, len);
  free(buf);

  test_cleanup_files();
}

TEST_CASE("VFS: copy file", "[vfs_basic]") {
  test_cleanup_files();

  const char *content = "Source content for copy";
  size_t len = strlen(content);
  TEST_ASSERT_EQUAL(ESP_OK, vfs_helper_write_file_from_ram(
                                TEST_FILE1, (uint8_t *)content, len));
  TEST_ASSERT_EQUAL(ESP_OK, vfs_helper_copy_file(TEST_FILE1, TEST_COPY));

  uint8_t *copied = NULL;
  size_t copied_len;
  TEST_ASSERT_EQUAL(
      ESP_OK, vfs_helper_read_file_to_ram(TEST_COPY, &copied, &copied_len));
  TEST_ASSERT_EQUAL(len, copied_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(content, copied, len);
  free(copied);

  test_cleanup_files();
}

TEST_CASE("VFS: read buffer allocation", "[vfs_basic]") {
  test_cleanup_files();

  const uint8_t data[256] = {0};
  TEST_ASSERT_EQUAL(
      ESP_OK, vfs_helper_write_file_from_ram(TEST_FILE1, data, sizeof(data)));

  uint8_t *buf = NULL;
  size_t len;
  TEST_ASSERT_EQUAL(ESP_OK,
                    vfs_helper_read_file_to_ram(TEST_FILE1, &buf, &len));
  TEST_ASSERT_EQUAL(sizeof(data), len);
  TEST_ASSERT_NOT_NULL(buf);
  free(buf);

  test_cleanup_files();
}