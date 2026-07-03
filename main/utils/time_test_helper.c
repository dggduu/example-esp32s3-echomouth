#include "time_test_helper.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static StackType_t *s_heap_monitor_stack = NULL;
static StaticTask_t s_heap_monitor_tcb;

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
  UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
  TaskStatus_t *task_array = malloc(num_tasks * sizeof(TaskStatus_t));
  if (task_array == NULL) {
    ESP_LOGE("DEBUG", "Failed to alloc memory for task status array");
    return;
  }

  uint32_t total_runtime;
  num_tasks = uxTaskGetSystemState(task_array, num_tasks, &total_runtime);

  // 输出 VOFA 格式
  printf("# Task Watermarks (CSV): Name,Priority,HighWaterMark_Bytes\n");

  for (UBaseType_t i = 0; i < num_tasks; i++) {
    uint32_t high_water = uxTaskGetStackHighWaterMark(task_array[i].xHandle);
    printf("WATERMARK,%s,%u,%lu\n", task_array[i].pcTaskName,
           (unsigned)task_array[i].uxBasePriority, high_water);
  }
  free(task_array);
}

/*
 * 堆内存曲线记录任务
 **/
static void heap_monitor_task(void *arg) {
  uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
  TickType_t delay_ticks = pdMS_TO_TICKS(interval_ms);
  if (delay_ticks == 0)
    delay_ticks = 1;

  printf("=== HEAP MONITOR START (CSV) ===\n");
  printf("format,timestamp_us,free_heap_bytes\n");

  while (1) {
    int64_t now = esp_timer_get_time();
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    printf("HEAP_MON {\"t_us\":%lld,\"free\":%zu}\n", now, free_heap);
    vTaskDelay(delay_ticks);
  }
}

void debug_start_heap_monitor(uint32_t interval_ms) {
  s_heap_monitor_stack =
      heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_heap_monitor_stack == NULL) {
    ESP_LOGE("DEBUG", "Failed to alloc heap monitor stack in PSRAM");
    return;
  }

  // 使用静态方式创建任务
  TaskHandle_t handle = xTaskCreateStatic(
      heap_monitor_task, "heap_mon", 4096 / sizeof(StackType_t),
      (void *)(uintptr_t)interval_ms, configMAX_PRIORITIES - 2,
      s_heap_monitor_stack, &s_heap_monitor_tcb);

  if (handle == NULL) {
    ESP_LOGE("DEBUG", "Failed to create heap monitor task (static)");
    // 创建失败，释放已分配的 PSRAM 栈
    heap_caps_free(s_heap_monitor_stack);
    s_heap_monitor_stack = NULL;
  }
}