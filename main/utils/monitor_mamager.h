#ifndef MONITOR_TASK_H
#define MONITOR_TASK_H

void monitor_task_start(void);
void monitor_task_reset_timer(void);

// 快捷功能
void monitor_task_pause(void);
void monitor_task_resume(void);

#endif