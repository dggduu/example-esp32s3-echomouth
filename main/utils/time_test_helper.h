#ifndef __DEBUG_HELPER_H__
#define __DEBUG_HELPER_H__

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>

#include "nvs_helper.h"

#include "monitor_mamager.h"
#include "task_manager.h"
#define LOG_CLR_RESET "\033[0m"
#define LOG_CLR_CYAN "\033[0;36m"
#define LOG_CLR_PURPLE "\033[0;35m"

/**
 * @brief 打印当前内存快照
 * 区分内部RAM（DMA安全）和外部PSRAM
 */
#define TEST_MEM_INFO(tag)                                                     \
  do {                                                                         \
    size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);            \
    size_t ext_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);              \
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);     \
    printf(LOG_CLR_CYAN "[MEM][%s] Internal: %zu B | PSRAM: %zu B | Min Ever " \
                        "Free: %zu B\n" LOG_CLR_RESET,                         \
           tag, int_free, ext_free, min_free);                                 \
  } while (0)

/**
 * @brief 性能测试宏：包含耗时检测与内存波动检测
 * @param func 函数名
 */
#define TEST_TIME(func, ...)                                                   \
  do {                                                                         \
    size_t _mem_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);          \
    int64_t _start = esp_timer_get_time();                                     \
    func(__VA_ARGS__);                                                         \
    int64_t _end = esp_timer_get_time();                                       \
    size_t _mem_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);           \
    printf(LOG_CLR_PURPLE "[PERF] %s: %lld us | Mem Δ: %d B\n" LOG_CLR_RESET,  \
           #func, (_end - _start), (int)(_mem_after - _mem_before));           \
  } while (0)

/**
 * @brief 检查指针是否在PSRAM中
 */
#define CHECK_IS_PSRAM(ptr)                                                    \
  do {                                                                         \
    bool is_spi = esp_ptr_external_ram(ptr);                                   \
    printf("[CHECK] Ptr %p is in %s\n", ptr,                                   \
           is_spi ? "PSRAM" : "Internal RAM");                                 \
  } while (0)

/**
 * @brief 打印当前NVS 中的did 与pid
 *
 */
void test_nvs_info(void);

void test_task_monitor();

// ————— 任务堆栈高水位 —————
/**
 * @brief 打印所有 FreeRTOS 任务的高水位线（剩余堆栈字节数）
 *        内部使用 uxTaskGetSystemState 确保准确性
 * @note  需在 menuconfig 中开启 FreeRTOS 的 trace 功能：
 *        Component config --> FreeRTOS --> Kernel --> Enable FreeRTOS trace
 * facility
 */
void debug_print_task_watermarks(void);

// ————— 堆内存曲线记录 —————
/**
 * @brief 启动堆内存监控任务（长期运行）
 *        每隔 'interval_ms' 毫秒输出一行 CSV：
 *        "HEAP,<timestamp_us>,<free_heap_bytes>"
 * @param interval_ms 采样间隔（毫秒）
 * @note  可通过串口工具重定向到文件，再用脚本绘图
 */
void debug_start_heap_monitor(uint32_t interval_ms);

#endif