
#include "time_test_helper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void test_nvs_info(void) {

  TEST_MEM_INFO("NVS_READ_START");

  int32_t did = 0;
  int32_t pid = 0;

  did = nvs_helper_get_did();

  pid = nvs_helper_get_pid();

  if (did == -1 || pid == -1) {
    ESP_LOGE("NVS", "Failed to fetch DID/PID from NVS! Using defaults.");
  } else {
    ESP_LOGI("NVS", "Successfully loaded -> DID: %ld | PID: %ld", did, pid);
  }

  TEST_MEM_INFO("NVS_READ_END");
}

void test_task_monitor() {
  int32_t task_id = task_manager_get_active_id();
  printf(LOG_CLR_PURPLE "[DEBUG]:test task moinitr,task id:%ld\n", task_id);
}

/*
 * 打印所有任务堆栈高水位
 **/
void debug_print_task_watermarks(void) {
  // 获取系统当前的任务总数
  UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
  // 动态分配数组用于保存每个任务的状态快照
  TaskStatus_t *task_array = malloc(num_tasks * sizeof(TaskStatus_t));
  if (task_array == NULL) {
    ESP_LOGE("DEBUG", "Failed to alloc memory for task status array");
    return;
  }

  uint32_t total_runtime;
  num_tasks = uxTaskGetSystemState(task_array, num_tasks, &total_runtime);

  printf(LOG_CLR_CYAN
         "========== Task Stack Watermarks ==========\n" LOG_CLR_RESET);
  printf("%-20s %8s %12s\n", "Task Name", "Priority", "HighWaterMark(B)");
  printf("--------------------------------------------\n");

  for (UBaseType_t i = 0; i < num_tasks; i++) {
    // 高水位线：从创建以来堆栈剩余空间的最小值
    uint32_t high_water = uxTaskGetStackHighWaterMark(task_array[i].xHandle);
    printf("%-20s %8u %12lu\n", task_array[i].pcTaskName,
           (unsigned)task_array[i].uxBasePriority, high_water);
  }
  printf("============================================\n");

  free(task_array);
}

/*
 * 堆内存曲线记录任务
 **/
static void heap_monitor_task(void *arg) {
  uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
  TickType_t delay_ticks = pdMS_TO_TICKS(interval_ms);
  if (delay_ticks == 0)
    delay_ticks = 1; // 至少1个tick

  printf("=== HEAP MONITOR START (CSV) ===\n");
  printf("format,timestamp_us,free_heap_bytes\n");

  while (1) {
    int64_t now = esp_timer_get_time(); // 微秒级时间戳
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    printf("HEAP,%lld,%zu\n", now, free_heap);
    vTaskDelay(delay_ticks);
  }
}

void debug_start_heap_monitor(uint32_t interval_ms) {
  BaseType_t ret = xTaskCreate(heap_monitor_task, "heap_mon", 4096,
                               (void *)(uintptr_t)interval_ms,
                               configMAX_PRIORITIES - 2, NULL);
  if (ret != pdPASS) {
    ESP_LOGE("DEBUG", "Failed to create heap monitor task");
  }
}