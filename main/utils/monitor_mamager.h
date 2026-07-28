#ifndef MONITOR_TASK_H
#define MONITOR_TASK_H

#include "stdbool.h"

void monitor_task_start(void);
void monitor_task_reset_timer(void);

// 快捷功能
void monitor_task_pause(void);
void monitor_task_resume(void);

// 强制触发上传 (debug 用)
void monitor_task_force_capture(bool skip_face);

#endif