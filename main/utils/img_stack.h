#ifndef __IMG_STACK_HEADER__
#define __IMG_STACK_HEADER__

#include "cam_shared.h"
#include <stdbool.h>

void img_stack_init(void);
bool img_stack_push(const img_job_t *job);
bool img_stack_peek(img_job_t *out_job); // 查看栈顶但不弹出
bool img_stack_commit(void);             // 上传成功后，确认弹出并丢弃栈顶
void img_stack_update_retry(const img_job_t *job);
#endif // !__img_stack
