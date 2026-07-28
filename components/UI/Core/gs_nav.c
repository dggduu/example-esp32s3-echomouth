#include "gs_nav.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STACK_DEPTH 8

typedef struct {
  const gs_page_desc_t *desc;
  void *ctx;
} stack_item_t;

typedef struct {
  const gs_page_desc_t *page;
  void *args;
} gs_nav_async_payload_t;

static struct {
  stack_item_t stack[MAX_STACK_DEPTH];
  int8_t top;
  lv_obj_t *container;
  bool busy;
} s_nav = {.top = -1, .busy = false, .container = NULL};

static void render_page_internal(int idx) {
  if (idx < 0 || idx > s_nav.top || !s_nav.container) {
    return;
  }
  lv_anim_del(NULL, NULL);
  lv_obj_clean(s_nav.container);

  if (s_nav.stack[idx].desc && s_nav.stack[idx].desc->render_cb) {
    lv_obj_t *page =
        s_nav.stack[idx].desc->render_cb(s_nav.container, s_nav.stack[idx].ctx);
    if (page) {
      lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    }
  }
}

void gs_nav_init(lv_obj_t *container) {
  if (!container)
    return;

  // 假设外部调用已在 lvgl_port_lock 保护下，或者自行加锁
  s_nav.container = container;
  lv_obj_clean(container);
  s_nav.top = -1;
  s_nav.busy = false;
}

/* 核心无锁 Push 实现（供内部或已持锁的 Task 调用） */
static int gs_nav_push_unlocked(const gs_page_desc_t *page, void *args) {
  if (!s_nav.container || !page || s_nav.busy)
    return -1;
  if (s_nav.top + 1 >= MAX_STACK_DEPTH)
    return -1;

  ESP_LOGI("NAV", "push page");
  s_nav.busy = true;

  void *ctx = NULL;
  if (page->init_cb) {
    ctx = page->init_cb(args);
    if (ctx == NULL) {
      s_nav.busy = false;
      return -1;
    }
  }

  s_nav.top++;
  s_nav.stack[s_nav.top].desc = page;
  s_nav.stack[s_nav.top].ctx = ctx;

  render_page_internal(s_nav.top);

  s_nav.busy = false;
  return 0;
}

/* 核心无锁 Pop 实现 */
static int gs_nav_pop_unlocked(void) {
  if (s_nav.top < 0 || s_nav.busy)
    return -1;

  s_nav.busy = true;

  const gs_page_desc_t *cur_desc = s_nav.stack[s_nav.top].desc;
  if (cur_desc && cur_desc->deinit_cb) {
    cur_desc->deinit_cb(s_nav.stack[s_nav.top].ctx);
  }

  s_nav.top--;

  if (s_nav.top >= 0) {
    render_page_internal(s_nav.top);
  } else {
    lv_obj_clean(s_nav.container);
  }

  s_nav.busy = false;
  return 0;
}

/* 供 App 业务线程（如 app_main）同步调用的接口（自动加锁） */
int gs_nav_push(const gs_page_desc_t *page, void *args) {
  int ret = -1;
  if (lvgl_port_lock(-1)) {
    ret = gs_nav_push_unlocked(page, args);
    lvgl_port_unlock();
  }
  return ret;
}

int gs_nav_pop(void) {
  int ret = -1;
  if (lvgl_port_lock(-1)) {
    ret = gs_nav_pop_unlocked();
    lvgl_port_unlock();
  }
  return ret;
}

void gs_nav_loop(void) {
  // 必须在 LVGL 锁保护下（例如在 nav_loop_task 内部）调用
  if (s_nav.top < 0 || s_nav.busy)
    return;

  stack_item_t *current = &s_nav.stack[s_nav.top];
  if (current->desc && current->desc->update_cb && current->ctx) {
    current->desc->update_cb(current->ctx);
  }
}

/* Async 回调在 LVGL 线程内部执行，本身已持锁，直接调用 unlocked 版本！ */
static void gs_nav_push_async_cb(void *p) {
  gs_nav_async_payload_t *payload = (gs_nav_async_payload_t *)p;
  if (payload) {
    gs_nav_push_unlocked(payload->page, payload->args);
    free(payload);
  }
}

static void gs_nav_pop_async_cb(void *p) {
  (void)p;
  gs_nav_pop_unlocked();
}

void gs_nav_push_async(const gs_page_desc_t *page, void *args) {
  if (!page)
    return;

  gs_nav_async_payload_t *payload = malloc(sizeof(gs_nav_async_payload_t));
  if (payload) {
    payload->page = page;
    payload->args = args;
    lv_async_call(gs_nav_push_async_cb, payload);
  }
}

void gs_nav_pop_async(void) { lv_async_call(gs_nav_pop_async_cb, NULL); }

int gs_nav_depth(void) { return s_nav.top + 1; }