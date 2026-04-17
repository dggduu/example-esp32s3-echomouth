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
  int64_t deadline;
  int likes;
} task_item_t;

typedef struct {
  int32_t deviceId;
  int page;
  int task_count;
  bool has_more;
  task_item_t tasks[MAX_TASKS];

  lv_obj_t *main_cont;
  lv_obj_t *btn_prev;
  lv_obj_t *btn_next;
  lv_obj_t *lbl_page;

} page_todo_ctx_t;

bool task_manager_init(int32_t device_id);

bool task_manager_fetch_list(page_todo_ctx_t *ctx);
const char *task_manager_get_active_title(void);
bool task_manager_start(int task_id, const char *title);

time_t task_manager_get_start_time(void);

bool task_manager_complete(int task_id);

int32_t task_manager_get_active_id(void);

#endif