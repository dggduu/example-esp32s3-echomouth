#include "StyleSheet.h"
#include "core/lv_obj_style_gen.h"
#include "esp_lvgl_port.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "lv_conf_internal.h"
#include "monitor_mamager.h"
#include "nvs_helper.h"
#include "task_manager.h"
#include "ui_circle_toolkit.h"
#include "ui_circle_utils.h"
#include <string.h>
#include <time.h>

#define TAG "PAGE_TODO"

static page_todo_ctx_t s_ctx;
static lv_obj_t *s_spinner = NULL; // 异步加载动画句柄

LV_FONT_DECLARE(chili_cn);

/* ---------------------------------------------------------------
 * 前置函数声明
 * --------------------------------------------------------------*/
static void on_start_click(lv_event_t *e);
static void on_complete_click(lv_event_t *e);
static void load_and_render_async(void);

/* 辅助颜色与格式化函数 */
static lv_color_t get_status_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return S_COLOR_PRIMARY_CONTAINER;
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0xE8F5E9);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xFFF3E0);
  if (strcmp(status, "rejected") == 0)
    return S_COLOR_ERROR_CONTAINER;
  return S_COLOR_SURFACE_CONTAINER;
}

static lv_color_t get_status_text_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return S_COLOR_ON_PRIMARY_CONTAINER;
  if (strcmp(status, "completed") == 0)
    return lv_color_hex(0x2E7D32);
  if (strcmp(status, "pending_review") == 0)
    return lv_color_hex(0xE65100);
  if (strcmp(status, "rejected") == 0)
    return S_COLOR_ERROR;
  return S_TEXT_SECONDARY;
}

static lv_color_t get_status_btn_color(const char *status) {
  if (strcmp(status, "active") == 0)
    return lv_color_hex(0x2E7D32);
  if (strcmp(status, "rejected") == 0)
    return S_COLOR_ERROR;
  return S_COLOR_PRIMARY;
}

static void format_deadline(int64_t deadline_sec, char *buffer,
                            size_t buf_size) {
  if (deadline_sec <= 0) {
    snprintf(buffer, buf_size, "无截止");
    return;
  }
  time_t seconds = (time_t)deadline_sec;
  struct tm tm_info;
  localtime_r(&seconds, &tm_info);
  strftime(buffer, buf_size, "%m-%d %H:%M", &tm_info);
}

/*---------------------------------------------------------------
 * Loading 状态控制
 *--------------------------------------------------------------*/
static void show_loading(bool show) {
  if (lvgl_port_lock(0)) {
    if (show) {
      if (!s_spinner && s_ctx.main_cont) {
        /* spinner 阶段临时切回 CENTER 保持居中显示 */
        lv_obj_set_flex_align(s_ctx.main_cont, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_spinner = ui_circle_create_spinner(s_ctx.main_cont);
      }
    } else {
      if (s_spinner) {
        lv_obj_del(s_spinner);
        s_spinner = NULL;
      }
    }
    lvgl_port_unlock();
  }
}

/*---------------------------------------------------------------
 * UI 渲染（主线程安全调用）
 *--------------------------------------------------------------*/
static void update_pagination_ui(page_todo_ctx_t *ctx) {
  lv_label_set_text_fmt(ctx->lbl_page, "%d", ctx->page + 1);

  if (ctx->page > 0) {
    lv_obj_clear_state(ctx->btn_prev, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(ctx->btn_prev, LV_STATE_DISABLED);
  }

  if (ctx->has_more) {
    lv_obj_clear_state(ctx->btn_next, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(ctx->btn_next, LV_STATE_DISABLED);
  }
}

static void render_list_async_cb(void *arg) {
  bool success = (bool)(intptr_t)arg;
  show_loading(false);

  if (!lvgl_port_lock(0))
    return;

  if (!success) {
    gs_portal_toast_show(
        (gs_toast_config_t){.msg = "获取任务失败", .type = GS_TOAST_FAILED});
    lvgl_port_unlock();
    return;
  }

  page_todo_ctx_t *ctx = &s_ctx;

  /* 数据渲染阶段恢复主轴 START（spinner 时是 CENTER），
   * 保证内容溢出时第一个元素位于容器顶部、可正常滚动到 */
  lv_obj_set_flex_align(ctx->main_cont, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_clean(ctx->main_cont);

  /* 空数据处理 (Empty State) */
  if (ctx->task_count == 0) {
    lv_obj_t *empty_box = lv_obj_create(ctx->main_cont);
    lv_obj_set_size(empty_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(empty_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(empty_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(empty_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(empty_box, 0, 0);

    lv_obj_t *icon = lv_label_create(empty_box);
    lv_label_set_text(icon, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(icon, S_TEXT_SECONDARY, 0);

    lv_obj_t *lbl_empty = lv_label_create(empty_box);
    lv_label_set_text(lbl_empty, "暂无待办任务");
    lv_obj_set_style_text_font(lbl_empty, &chili_cn, 0);
    lv_obj_set_style_text_color(lbl_empty, S_TEXT_SECONDARY, 0);
    lv_obj_set_style_margin_top(lbl_empty, 6, 0);

    update_pagination_ui(ctx);
    lvgl_port_unlock();
    return;
  }

  for (int i = 0; i < ctx->task_count; i++) {
    task_item_t *item = &ctx->tasks[i];

    /* 任务卡片 */
    lv_obj_t *card = lv_obj_create(ctx->main_cont);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(card, get_status_color(item->status), 0);
    lv_obj_set_style_radius(card, S_RADIUS_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(card, 6, 0);
    lv_obj_set_style_shadow_offset_y(card, 2, 0);

    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* 标题 */
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, item->title);
    lv_obj_set_style_text_font(lbl_title, &chili_cn, 0);
    lv_obj_set_style_text_color(lbl_title, S_TEXT_PRIMARY, 0);

    /* 点赞数 — 家长在 App 端点赞后孩子端同步显示 */
    if (item->likes > 0) {
      lv_obj_t *lbl_likes = lv_label_create(card);
      lv_label_set_text_fmt(lbl_likes, "已点赞");
      lv_obj_set_style_text_color(lbl_likes, lv_color_hex(0xEF4444), 0);
      lv_obj_set_style_text_font(lbl_likes, &chili_cn, 0);
      lv_obj_set_style_margin_top(lbl_likes, 2, 0);
    }

    /* 截止时间 */
    char deadline_str[32];
    format_deadline(item->deadline, deadline_str, sizeof(deadline_str));
    lv_obj_t *lbl_deadline = lv_label_create(card);
    lv_label_set_text_fmt(lbl_deadline, "截止: %s", deadline_str);
    lv_obj_set_style_text_font(lbl_deadline, &chili_cn, 0);
    lv_obj_set_style_text_color(lbl_deadline, S_TEXT_SECONDARY, 0);
    lv_obj_set_style_margin_top(lbl_deadline, 2, 0);

    /* 操作按钮 / 状态 Badge */
    if (strcmp(item->status, "pending") == 0 ||
        strcmp(item->status, "active") == 0 ||
        strcmp(item->status, "rejected") == 0) {

      lv_obj_t *btn = lv_btn_create(card);
      lv_obj_set_style_radius(btn, S_RADIUS_BTN, 0);
      lv_obj_set_style_bg_color(btn, get_status_btn_color(item->status), 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      lv_obj_set_style_pad_hor(btn, 20, 0);
      lv_obj_set_style_pad_ver(btn, 4, 0);
      lv_obj_set_style_margin_top(btn, 6, 0);

      lv_obj_t *btn_lbl = lv_label_create(btn);
      lv_obj_set_style_text_font(btn_lbl, &chili_cn, 0);
      lv_obj_set_style_text_color(btn_lbl, S_TEXT_ON_DARK, 0);

      if (strcmp(item->status, "pending") == 0) {
        lv_label_set_text(btn_lbl, "开始");
        lv_obj_add_event_cb(btn, on_start_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
      } else if (strcmp(item->status, "active") == 0) {
        lv_label_set_text(btn_lbl, "提交");
        lv_obj_add_event_cb(btn, on_complete_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
      } else if (strcmp(item->status, "rejected") == 0) {
        lv_label_set_text(btn_lbl, "重新提交");
        lv_obj_add_event_cb(btn, on_complete_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
      }
    } else {
      /* 胶囊状态 Badge */
      lv_obj_t *badge = lv_obj_create(card);
      lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(badge, get_status_text_color(item->status), 0);
      lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
      lv_obj_set_style_pad_hor(badge, 12, 0);
      lv_obj_set_style_pad_ver(badge, 3, 0);
      lv_obj_set_style_border_width(badge, 0, 0);
      lv_obj_set_style_margin_top(badge, 6, 0);

      lv_obj_t *badge_txt = lv_label_create(badge);
      const char *txt =
          strcmp(item->status, "completed") == 0 ? "已完成" : "审核中";
      lv_label_set_text(badge_txt, txt);
      lv_obj_set_style_text_font(badge_txt, &chili_cn, 0);
      lv_obj_set_style_text_color(badge_txt,
                                  get_status_text_color(item->status), 0);
    }
  }

  update_pagination_ui(ctx);
  lvgl_port_unlock();
}

/*---------------------------------------------------------------
 * 异步后台 Fetch 任务
 *--------------------------------------------------------------*/
static void async_fetch_task(void *pvParameters) {
  bool success = task_manager_fetch_list(&s_ctx);
  lv_async_call(render_list_async_cb, (void *)(intptr_t)success);
  vTaskDelete(NULL);
}

static void load_and_render_async(void) {
  show_loading(true);
  xTaskCreate(async_fetch_task, "async_fetch", 4096, NULL, 5, NULL);
}

/*---------------------------------------------------------------
 * 异步 Start 任务点击逻辑
 *--------------------------------------------------------------*/
typedef struct {
  int task_id;
  char title[64];
} start_task_args_t;

static void start_task_result_cb(void *arg) {
  bool success = (bool)(intptr_t)arg;
  show_loading(false);

  if (success) {
    gs_toast_show("任务已开始", GS_TOAST_SUCCESS);
    load_and_render_async();
  } else {
    gs_toast_show("请先完成当前任务", GS_TOAST_FAILED);
  }
}

static void async_start_task(void *pvParameters) {
  start_task_args_t *args = (start_task_args_t *)pvParameters;
  bool success = task_manager_start(args->task_id, args->title);
  free(args);

  lv_async_call(start_task_result_cb, (void *)(intptr_t)success);
  vTaskDelete(NULL);
}

static void on_start_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);

  start_task_args_t *args = malloc(sizeof(start_task_args_t));
  if (!args) {
    gs_toast_show("内存不足", GS_TOAST_FAILED);
    return;
  }
  args->task_id = s_ctx.tasks[index].id;
  strlcpy(args->title, s_ctx.tasks[index].title, sizeof(args->title));

  show_loading(true);
  xTaskCreate(async_start_task, "async_start", 3072, args, 5, NULL);
}

/*---------------------------------------------------------------
 * 其他事件与页面生命周期
 *--------------------------------------------------------------*/
extern const gs_page_desc_t page_cam;
#include "cam_shared.h"

static void on_complete_click(lv_event_t *e) {
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  int task_id = s_ctx.tasks[index].id;

  cam_page_args_t *args =
      heap_caps_malloc(sizeof(cam_page_args_t), MALLOC_CAP_INTERNAL);
  if (args) {
    args->task_id = task_id;
    args->device_id = s_ctx.deviceId;
    gs_nav_push(&page_cam, args);
  } else {
    gs_toast_show("内存不足", GS_TOAST_FAILED);
  }
}

static void btn_next_event(lv_event_t *e) {
  s_ctx.page++;
  load_and_render_async();
}

static void btn_prev_event(lv_event_t *e) {
  if (s_ctx.page > 0) {
    s_ctx.page--;
    load_and_render_async();
  }
}

static void btn_back_event(lv_event_t *e) { gs_nav_pop(); }

static void *page_todo_init(void *args) {
  memset(&s_ctx, 0, sizeof(s_ctx));
  s_ctx.page = 0;
  int32_t dev_id = 1;
  nvs_helper_get_i32("storage", "device_id", &dev_id);
  task_manager_init(dev_id);
  s_ctx.deviceId = dev_id;
  return &s_ctx;
}

static void page_todo_deinit(void *ctx) {
  s_spinner = NULL;
  (void)ctx;
}

static lv_obj_t *page_todo_render(lv_obj_t *parent, void *ctx_ptr) {
  page_todo_ctx_t *ctx = (page_todo_ctx_t *)ctx_ptr;

  const int16_t TOP_BAR_H = 48;
  const int16_t BOTTOM_BAR_H = 48;

  int16_t top_safe_pad = ui_circle_get_safe_pad(0, TOP_BAR_H);
  if (top_safe_pad < 16)
    top_safe_pad = 16;

  int16_t bottom_safe_pad =
      ui_circle_get_safe_pad(UI_SCREEN_HEIGHT - BOTTOM_BAR_H, BOTTOM_BAR_H);
  if (bottom_safe_pad < 16)
    bottom_safe_pad = 16;

  /* 1. 全屏根容器 */
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_bg_color(root, S_COLOR_BACKGROUND, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  /* 2. 顶栏 (直接调用 utils 极简生成退出键与居中标题) */
  lv_obj_t *top_bar = lv_obj_create(root);
  lv_obj_set_size(top_bar, LV_PCT(100), TOP_BAR_H);
  lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_bar, 0, 0);
  lv_obj_set_style_pad_hor(top_bar, top_safe_pad, 0);
  lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

  // 【左侧中央退出键】：工具函数一行搞定

  // 【居中标题】：容纳四字“待办事项”
  lv_obj_t *title = lv_label_create(top_bar);
  lv_label_set_text(title, "待办事项");
  lv_obj_set_style_text_font(title, &chili_cn, 0);
  lv_obj_set_style_text_color(title, S_COLOR_ON_SURFACE, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  /* 3. 中间卡片/加载区域 */
  int16_t content_safe_pad = ui_circle_get_safe_pad(
      TOP_BAR_H, UI_SCREEN_HEIGHT - TOP_BAR_H - BOTTOM_BAR_H);
  if (content_safe_pad < 12)
    content_safe_pad = 12;

  ctx->main_cont = lv_obj_create(root);
  lv_obj_set_width(ctx->main_cont, LV_PCT(100));
  lv_obj_set_flex_grow(ctx->main_cont, 1);
  lv_obj_set_flex_flow(ctx->main_cont, LV_FLEX_FLOW_COLUMN);

  /* 主轴用 START（默认列表语义）：
   * 若用 CENTER，内容高于容器时 place_content 会把卡片起始位推到
   * 容器顶边之上（负坐标），而 LVGL 拖动滚动在顶部边界处强制 diff=0，
   * 第一个元素永远滚不出来（lv_indev_scroll.c elastic_diff）。
   * 加载中 spinner 需要居中，见 show_loading()。 */
  lv_obj_set_flex_align(ctx->main_cont, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_bg_opa(ctx->main_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_hor(ctx->main_cont, content_safe_pad, 0);
  lv_obj_set_style_pad_ver(ctx->main_cont, 4, 0);
  lv_obj_set_style_border_width(ctx->main_cont, 0, 0);
  lv_obj_set_style_pad_gap(ctx->main_cont, 8, 0);

  /* 4. 底部分页栏 (纯粹的分页控制) */
  lv_obj_t *bottom_bar = lv_obj_create(root);
  lv_obj_set_size(bottom_bar, LV_PCT(100), BOTTOM_BAR_H);
  lv_obj_set_flex_flow(bottom_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bottom_bar, 0, 0);
  lv_obj_set_style_pad_hor(bottom_bar, bottom_safe_pad, 0);
  lv_obj_set_style_pad_gap(bottom_bar, 16, 0);
  lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

  // 上一页
  ctx->btn_prev =
      ui_circle_create_fab(bottom_bar, LV_SYMBOL_PREV,
                           S_COLOR_SURFACE_CONTAINER_HIGH, S_COLOR_ON_SURFACE);
  lv_obj_set_style_opa(ctx->btn_prev, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_add_event_cb(ctx->btn_prev, btn_prev_event, LV_EVENT_CLICKED, NULL);

  // 页码
  ctx->lbl_page = lv_label_create(bottom_bar);
  lv_obj_set_style_text_color(ctx->lbl_page, S_COLOR_ON_SURFACE, 0);
  lv_obj_set_style_text_align(ctx->lbl_page, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(ctx->lbl_page, "1");

  // 下一页
  ctx->btn_next =
      ui_circle_create_fab(bottom_bar, LV_SYMBOL_NEXT,
                           S_COLOR_SURFACE_CONTAINER_HIGH, S_COLOR_ON_SURFACE);
  lv_obj_set_style_opa(ctx->btn_next, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_add_event_cb(ctx->btn_next, btn_next_event, LV_EVENT_CLICKED, NULL);
  ui_circle_add_exit_btn(root, btn_back_event);
  /* 5. 开始加载数据 */
  load_and_render_async();

  return root;
}

const gs_page_desc_t page_todo = {
    .init_cb = page_todo_init,
    .render_cb = page_todo_render,
    .update_cb = NULL,
    .deinit_cb = page_todo_deinit,
};