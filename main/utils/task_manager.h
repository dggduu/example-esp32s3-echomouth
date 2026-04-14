#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include <time.h>

#define MAX_TASKS 3
#define TASK_STR_LEN 64

typedef struct {
  int id;
  char title[TASK_STR_LEN];
  char desc[TASK_STR_LEN];
  char status[16];
} task_item_t;

typedef struct {
  int32_t deviceId;
  int page;
  int task_count;
  bool has_more;
  task_item_t tasks[MAX_TASKS];

  lv_obj_t *main_cont; // 修改为 main 容器
  lv_obj_t *btn_prev;  // 新增上一页按钮
  lv_obj_t *btn_next;
  lv_obj_t *lbl_page; // 新增页码显示

} page_todo_ctx_t;

// --- 核心接口 ---

/**
 * @brief 初始化管理器，从 NVS 加载持久化状态
 */
bool task_manager_init(int32_t device_id);

/**
 * @brief 获取任务列表 (业务分页)
 */
bool task_manager_fetch_list(page_todo_ctx_t *ctx);

/**
 * @brief 开始一个新任务
 * @return false 如果已有任务在运行或网络失败
 */
const char *task_manager_get_active_title(void);
bool task_manager_start(int task_id, const char *title);

time_t task_manager_get_start_time(void);
/**
 * @brief 完成当前任务
 * @return false 如果 ID 不匹配或服务器返回失败
 */
bool task_manager_complete(int task_id);

/**
 * @brief 获取当前正在进行的任务ID
 * @return 0 表示无任务
 */
int32_t task_manager_get_active_id(void);

#endif