#pragma once
#include <stdbool.h>

#define MAX_IMG_STACK 10
#define IMG_PATH_LEN 128

void img_stack_init(void);
bool img_stack_push(const char *path);
bool img_stack_pop(char *out_path);
int img_stack_size(void);
