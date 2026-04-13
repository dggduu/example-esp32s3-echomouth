
#include "time_test_helper.h"
/**
 * @brief 打印当前NVS 中的did 与pid
 *
 */
void test_nvs_info(void) {
  // 监控读取前的内存状态
  TEST_MEM_INFO("NVS_READ_START");

  int32_t did = 0;
  int32_t pid = 0;

  // 获取 DID
  did = nvs_helper_get_did();
  // 获取 PID
  pid = nvs_helper_get_pid();

  if (did == -1 || pid == -1) {
    ESP_LOGE("NVS", "Failed to fetch DID/PID from NVS! Using defaults.");
  } else {
    ESP_LOGI("NVS", "Successfully loaded -> DID: %ld | PID: %ld", did, pid);
  }

  // 监控读取后的内存状态
  TEST_MEM_INFO("NVS_READ_END");
}

void test_task_monitor() {
  int32_t task_id = task_manager_get_active_id();
  printf(LOG_CLR_PURPLE "[DEBUG]:test task moinitr,task id:%ld\n", task_id);
}
