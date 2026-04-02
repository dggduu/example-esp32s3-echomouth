#include "img_stack.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>


static char stack[MAX_IMG_STACK][IMG_PATH_LEN];
static int top = -1;
static SemaphoreHandle_t mutex;

void img_stack_init(void) {
  mutex = xSemaphoreCreateMutex();
  top = -1;
}

bool img_stack_push(const char *path) {
  if (!mutex)
    return false;

  xSemaphoreTake(mutex, portMAX_DELAY);

  if (top >= MAX_IMG_STACK - 1) {
    xSemaphoreGive(mutex);
    return false;
  }

  top++;
  strncpy(stack[top], path, IMG_PATH_LEN);

  xSemaphoreGive(mutex);
  return true;
}

bool img_stack_pop(char *out_path) {
  if (!mutex)
    return false;

  xSemaphoreTake(mutex, portMAX_DELAY);

  if (top < 0) {
    xSemaphoreGive(mutex);
    return false;
  }

  strncpy(out_path, stack[top], IMG_PATH_LEN);
  top--;

  xSemaphoreGive(mutex);
  return true;
}

int img_stack_size(void) { return top + 1; }
