#include "gs_nav.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_STACK_DEPTH 8

/* 栈项 */
typedef struct {
  const gs_page_desc_t *desc;
  void *ctx; // 页面私有数据
} stack_item_t;

/* 全局状态 */
static struct {
  stack_item_t stack[MAX_STACK_DEPTH];
  int8_t top;          // 当前栈顶索引（-1 表示空）
  lv_obj_t *container; // 页面容器
  bool busy;           // 正在切换中，禁止操作
} s_nav = {.top = -1, .busy = false};

/* 内部函数：渲染指定栈项 */
static void render_page(int idx) {
  if (idx < 0 || idx > s_nav.top)
    return;
  lv_obj_clean(s_nav.container);
  if (s_nav.stack[idx].desc->render_cb) {
    lv_obj_t *page =
        s_nav.stack[idx].desc->render_cb(s_nav.container, s_nav.stack[idx].ctx);
    if (page) {
      lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
      lv_obj_move_to_index(page, 0);
    }
  }
}

void gs_nav_init(lv_obj_t *container) {
  if (!container)
    return;
  s_nav.container = container;
  // 清空容器
  lv_obj_clean(container);
  // 重置栈
  for (int i = 0; i <= s_nav.top; i++) {
    if (s_nav.stack[i].desc && s_nav.stack[i].desc->deinit_cb &&
        s_nav.stack[i].ctx) {
      s_nav.stack[i].desc->deinit_cb(s_nav.stack[i].ctx);
    }
  }
  s_nav.top = -1;
  s_nav.busy = false;
}

int gs_nav_push(const gs_page_desc_t *page, void *args) {
  if (!s_nav.container || !page)
    return -1;
  if (s_nav.busy)
    return -1; // 正在切换中，拒绝压栈
  if (s_nav.top + 1 >= MAX_STACK_DEPTH)
    return -1;

  s_nav.busy = true;

  // 初始化新页面
  void *ctx = NULL;
  if (page->init_cb) {
    ctx = page->init_cb(args);
    // 若 init_cb 返回 NULL 表示初始化失败，应停止压栈
    if (!ctx) {
      s_nav.busy = false;
      return -1;
    }
  }

  // 增加栈深度
  s_nav.top++;
  s_nav.stack[s_nav.top].desc = page;
  s_nav.stack[s_nav.top].ctx = ctx;

  // 渲染新页面
  render_page(s_nav.top);

  s_nav.busy = false;
  return 0;
}

int gs_nav_pop(void) {
  if (s_nav.top < 0)
    return -1;
  if (s_nav.busy)
    return -1; // 正在切换中，不允许 pop

  s_nav.busy = true;

  // 释放当前页资源
  const gs_page_desc_t *cur_desc = s_nav.stack[s_nav.top].desc;
  if (cur_desc->deinit_cb && s_nav.stack[s_nav.top].ctx) {
    cur_desc->deinit_cb(s_nav.stack[s_nav.top].ctx);
  }
  s_nav.top--;

  if (s_nav.top >= 0) {
    // 渲染上一页（无需重新初始化，ctx 仍有效）
    render_page(s_nav.top);
  } else {
    // 栈空，清空容器
    lv_obj_clean(s_nav.container);
  }

  s_nav.busy = false;
  return 0;
}

int gs_nav_depth(void) { return s_nav.top + 1; }