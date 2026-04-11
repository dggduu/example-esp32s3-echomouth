#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "lvgl.h"
#include "nvs_helper.h"
#include "task_manager.h"
#include <string.h>

#define TAG "PAGE_TODO"

static page_todo_ctx_t s_ctx;

/* ================= 样式辅助函数 (保持 Element Plus 风格) ================= */

static lv_color_t get_status_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0xECF5FF);
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0xF0F9EB);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xFDF6EC);
  if (strcmp(status, "rejected") == 0)
    return lv_color_hex(0xFEF0F0);
  return lv_color_hex(0xF4F4F5);
}

static lv_color_t get_status_text_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0x409EFF);
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0x67C23A);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xE6A23C);
  if (strcmp(status, "rejected") == 0)
    return lv_color_hex(0xF56C6C);
  return lv_color_hex(0x909399);
}

/* ================= 业务逻辑与 UI 刷新 ================= */

static void load_and_render(void); // 前置声明

static void on_start_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  int task_id = s_ctx.tasks[index].id;

  if (task_manager_start(task_id)) {
    gs_portal_toast_show((gs_toast_config_t){.msg = "Task started successfully",
                                             .type = GS_TOAST_SUCCESS});
    load_and_render(); // 刷新列表以更新状态
  } else {
    // 如果失败，可能是网络问题或已有任务在运行
    gs_portal_toast_show((gs_toast_config_t){
        .msg = "Fail: Another task is running", .type = GS_TOAST_FAILED});
  }
}

static void on_complete_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  int task_id = s_ctx.tasks[index].id;

  if (task_manager_complete(task_id)) {
    gs_portal_toast_show((gs_toast_config_t){.msg = "Task marked as complete",
                                             .type = GS_TOAST_SUCCESS});
    load_and_render();
  } else {
    gs_portal_toast_show((gs_toast_config_t){.msg = "Complete request failed",
                                             .type = GS_TOAST_FAILED});
  }
}

/* ================= UI 渲染逻辑 ================= */

static void update_pagination_ui(page_todo_ctx_t *ctx) {
  lv_label_set_text_fmt(ctx->lbl_page, "Page\n%d", ctx->page + 1);

  // 控制翻页按钮显隐
  if (ctx->page > 0)
    lv_obj_clear_flag(ctx->btn_prev, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(ctx->btn_prev, LV_OBJ_FLAG_HIDDEN);

  if (ctx->has_more)
    lv_obj_clear_flag(ctx->btn_next, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(ctx->btn_next, LV_OBJ_FLAG_HIDDEN);
}

static void render_list(page_todo_ctx_t *ctx) {
  lv_obj_clean(ctx->main_cont);

  for (int i = 0; i < ctx->task_count; i++) {
    task_item_t *item_data = &ctx->tasks[i];

    // 卡片容器
    lv_obj_t *card = lv_obj_create(ctx->main_cont);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(card, get_status_color(item_data->status), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 10, 0);

    // 标题行
    lv_obj_t *header = lv_obj_create(card);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, item_data->title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);

    lv_obj_t *lbl_status = lv_label_create(header);
    lv_label_set_text(lbl_status, item_data->status);
    lv_obj_set_style_text_color(lbl_status,
                                get_status_text_color(item_data->status), 0);

    // 描述
    lv_obj_t *lbl_desc = lv_label_create(card);
    lv_label_set_text(lbl_desc, item_data->desc);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(0x606266), 0);
    lv_obj_set_width(lbl_desc, LV_PCT(100));

    // 操作区
    lv_obj_t *btn_area = lv_obj_create(card);
    lv_obj_set_size(btn_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_area, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_area, 0, 0);
    lv_obj_set_style_border_width(btn_area, 0, 0);
    lv_obj_set_style_pad_all(btn_area, 0, 0);

    // 根据业务逻辑显示按钮
    if (strcmp(item_data->status, "pending") == 0) {
      lv_obj_t *btn = lv_btn_create(btn_area);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x409EFF), 0);
      lv_label_set_text(lv_label_create(btn), "Start");
      lv_obj_add_event_cb(btn, on_start_click, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
    } else if (strcmp(item_data->status, "active") == 0) {
      lv_obj_t *btn = lv_btn_create(btn_area);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x67C23A), 0);
      lv_label_set_text(lv_label_create(btn), "Complete");
      lv_obj_add_event_cb(btn, on_complete_click, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);
    }
  }
  update_pagination_ui(ctx);
}

static void load_and_render(void) {
  // 调用封装后的 Task Manager 接口
  if (task_manager_fetch_list(&s_ctx)) {
    if (lvgl_port_lock(0)) {
      render_list(&s_ctx);
      lvgl_port_unlock();
    }
  } else {
    gs_portal_toast_show((gs_toast_config_t){.msg = "Failed to fetch tasks",
                                             .type = GS_TOAST_FAILED});
  }
}

/* ================= 导航事件 ================= */

static void btn_next_event(lv_event_t *e) {
  s_ctx.page++;
  load_and_render();
}
static void btn_prev_event(lv_event_t *e) {
  if (s_ctx.page > 0) {
    s_ctx.page--;
    load_and_render();
  }
}
static void btn_back_event(lv_event_t *e) { gs_nav_pop(); }

/* ================= 页面生命周期 ================= */

static void *page_todo_init(void *args) {
  memset(&s_ctx, 0, sizeof(s_ctx));
  s_ctx.page = 0;

  int32_t dev_id = 1;
  nvs_helper_get_i32("storage", "device_id", &dev_id);

  // 初始化业务管理器
  task_manager_init(dev_id);
  s_ctx.deviceId = dev_id;

  return &s_ctx;
}

static void page_todo_deinit(void *ctx) {
  // 可以在此处做清理
}

static lv_obj_t *page_todo_render(lv_obj_t *parent, void *ctx_ptr) {
  page_todo_ctx_t *ctx = (page_todo_ctx_t *)ctx_ptr;

  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(root, 0, 0);

  // Left Panel
  lv_obj_t *side = lv_obj_create(root);
  lv_obj_set_size(side, 80, LV_PCT(100));
  lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(side, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(side, lv_color_hex(0x2C3E50), 0);
  lv_obj_set_style_radius(side, 0, 0);
  lv_obj_set_style_border_width(side, 0, 0);

  lv_obj_t *back = lv_btn_create(side);
  lv_label_set_text(lv_label_create(back), "< Back");
  lv_obj_add_event_cb(back, btn_back_event, LV_EVENT_CLICKED, NULL);

  lv_obj_t *ctrls = lv_obj_create(side);
  lv_obj_set_width(ctrls, LV_PCT(100));
  lv_obj_set_flex_flow(ctrls, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_opa(ctrls, 0, 0);
  lv_obj_set_style_border_width(ctrls, 0, 0);

  ctx->btn_prev = lv_btn_create(ctrls);
  lv_label_set_text(lv_label_create(ctx->btn_prev), "Prev");
  lv_obj_add_event_cb(ctx->btn_prev, btn_prev_event, LV_EVENT_CLICKED, NULL);

  ctx->lbl_page = lv_label_create(ctrls);
  lv_obj_set_style_text_color(ctx->lbl_page, lv_color_white(), 0);
  lv_obj_set_style_text_align(ctx->lbl_page, LV_TEXT_ALIGN_CENTER, 0);

  ctx->btn_next = lv_btn_create(ctrls);
  lv_label_set_text(lv_label_create(ctx->btn_next), "Next");
  lv_obj_add_event_cb(ctx->btn_next, btn_next_event, LV_EVENT_CLICKED, NULL);

  // Main Panel
  ctx->main_cont = lv_obj_create(root);
  lv_obj_set_height(ctx->main_cont, LV_PCT(100));
  lv_obj_set_flex_grow(ctx->main_cont, 1);
  lv_obj_set_flex_flow(ctx->main_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(ctx->main_cont, lv_color_hex(0xF0F2F5), 0);
  lv_obj_set_style_pad_all(ctx->main_cont, 10, 0);
  lv_obj_set_style_border_width(ctx->main_cont, 0, 0);

  load_and_render();

  return root;
}

const gs_page_desc_t page_todo = {
    .init_cb = page_todo_init,
    .render_cb = page_todo_render,
    .update_cb = NULL,
    .deinit_cb = page_todo_deinit,
};