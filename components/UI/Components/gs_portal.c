#include "gs_portal.h"
#include "easing.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>

/* ========== 1. 字体声明 ========== */
LV_FONT_DECLARE(chili_cn);

/* ========== 内部状态变量 ========== */
static lv_obj_t *g_current_toast = NULL;
static lv_obj_t *g_current_alert = NULL;
static TimerHandle_t g_toast_timer = NULL;

static gs_toast_config_t *g_toast_cfg = NULL;
static gs_alert_config_t *g_alert_cfg = NULL;

typedef struct {
  bool is_alert;
  union {
    gs_toast_config_t toast_cfg;
    gs_alert_config_t alert_cfg;
  };
} portal_create_params_t;

#define TOAST_ANIM_TIME_MS 300
#define TOAST_TOP_TARGET_Y 10
#define TOAST_TOP_START_Y -30

/* ========== 内部函数前置声明 ========== */
static void _toast_click_cb(lv_event_t *e);
static void _alert_ok_click(lv_event_t *e);
static void _alert_cancel_click(lv_event_t *e);
static void _async_create_cb(void *user_data);
static void _async_dismiss_cb(void *user_data);
static void toast_timer_cb(TimerHandle_t xTimer);

/* ========== 内存释放辅助函数 ========== */
static void _free_toast_cfg(gs_toast_config_t *cfg) {
  if (!cfg)
    return;
  if (cfg->msg) {
    free((void *)cfg->msg);
  }
  free(cfg);
}

static void _free_alert_cfg(gs_alert_config_t *cfg) {
  if (!cfg)
    return;
  if (cfg->title)
    free((void *)cfg->title);
  if (cfg->msg)
    free((void *)cfg->msg);
  free(cfg);
}

/* ========== 自定义 LVGL 动画属性 setter ========== */
static void anim_set_opa_cb(void *var, int32_t v) {
  if (lv_obj_is_valid((lv_obj_t *)var)) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
  }
}

static void anim_set_y_cb(void *var, int32_t v) {
  if (lv_obj_is_valid((lv_obj_t *)var)) {
    lv_obj_set_y((lv_obj_t *)var, (lv_coord_t)v);
  }
}

/* ========== 符合 LVGL 规范的 Path 回调函数 ========== */
static int32_t anim_path_quadratic_ease_out(const lv_anim_t *a) {
  uint32_t duration = a->duration;
  if (duration == 0)
    return a->end_value;

  AHFloat progress = (AHFloat)a->act_time / (AHFloat)duration;
  if (progress > 1.0f)
    progress = 1.0f;
  if (progress < 0.0f)
    progress = 0.0f;

  AHFloat eased = QuadraticEaseOut(progress);
  return a->start_value + (int32_t)((a->end_value - a->start_value) * eased);
}

static int32_t anim_path_quadratic_ease_in(const lv_anim_t *a) {
  uint32_t duration = a->duration;
  if (duration == 0)
    return a->end_value;

  AHFloat progress = (AHFloat)a->act_time / (AHFloat)duration;
  if (progress > 1.0f)
    progress = 1.0f;
  if (progress < 0.0f)
    progress = 0.0f;

  AHFloat eased = QuadraticEaseIn(progress);
  return a->start_value + (int32_t)((a->end_value - a->start_value) * eased);
}

/* Toast 消失动画完成回调：通过 anim->var 直接删除对应的 obj，防止指针错乱 */
static void _toast_exit_anim_ready_cb(lv_anim_t *a) {
  lv_obj_t *card = (lv_obj_t *)a->var;
  if (lv_obj_is_valid(card)) {
    lv_obj_t *root = lv_obj_get_parent(card);
    if (root) {
      if (root == g_current_toast) {
        g_current_toast = NULL;
      }
      lv_obj_delete(root);
    }
  }
}

/* ========== Toast 视图创建 ========== */
static lv_obj_t *_toast_view_create(lv_obj_t *parent, gs_toast_config_t *cfg) {
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);

  // 创建卡片容器
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
  lv_obj_set_style_text_color(card, lv_color_white(), 0);
  lv_obj_set_style_pad_all(card, 12, 0);

  // 背景色
  lv_color_t bg_color;
  switch (cfg->type) {
  case GS_TOAST_SUCCESS:
    bg_color = lv_color_hex(0xe1f3d8);
    break;
  case GS_TOAST_FAILED:
    bg_color = lv_color_hex(0xfcd3d3);
    break;
  default:
    bg_color = lv_color_hex(0xd9ecff);
    break;
  }
  lv_obj_set_style_bg_color(card, bg_color, 0);

  /* UTF-8 字符计数 */
  int char_count = 0;
  {
    const char *p = cfg->msg ? cfg->msg : "";
    while (*p) {
      if ((*p & 0xC0) != 0x80) char_count++; // 非续字节 = 新字符
      p++;
    }
  }

  lv_coord_t screen_w = lv_display_get_horizontal_resolution(NULL);
  lv_coord_t max_w = screen_w * 4 / 5;
  if (max_w > 280)
    max_w = 280;

  lv_obj_t *label = lv_label_create(card);
  lv_label_set_text(label, cfg->msg ? cfg->msg : "");
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, &chili_cn, 0);

  lv_coord_t lw, lh;

  if (char_count < 5) {
    /* 短文本：自适应宽度，单行不换行 */
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
  } else {
    /* 长文本：固定最大宽度，可变高度自动换行 */
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, max_w);
  }
  lv_obj_update_layout(label);
  lw = lv_obj_get_width(label);
  lh = lv_obj_get_height(label);

  /* 短文本如果意外超出也限制一下 */
  if (char_count < 5 && lw > max_w) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, max_w);
    lv_obj_update_layout(label);
    lw = lv_obj_get_width(label);
    lh = lv_obj_get_height(label);
  }

  lv_coord_t ph =
      lv_obj_get_style_pad_left(card, 0) + lv_obj_get_style_pad_right(card, 0);
  lv_coord_t pv =
      lv_obj_get_style_pad_top(card, 0) + lv_obj_get_style_pad_bottom(card, 0);
  lv_obj_set_size(card, lw + ph + 8, lh + pv);

  lv_obj_center(label);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, TOAST_TOP_START_Y);

  // 初始透明度设为 0
  lv_obj_set_style_opa(card, LV_OPA_TRANSP, 0);

  if (cfg->click_cb) {
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, _toast_click_cb, LV_EVENT_CLICKED, cfg);
  }

  /* ===== 启动进入弹出动画 ===== */
  lv_anim_t a_pos;
  lv_anim_init(&a_pos);
  lv_anim_set_var(&a_pos, card);
  lv_anim_set_values(&a_pos, TOAST_TOP_START_Y, TOAST_TOP_TARGET_Y);
  lv_anim_set_time(&a_pos, TOAST_ANIM_TIME_MS);
  lv_anim_set_exec_cb(&a_pos, anim_set_y_cb);
  lv_anim_set_path_cb(&a_pos, anim_path_quadratic_ease_out);
  lv_anim_start(&a_pos);

  lv_anim_t a_opa;
  lv_anim_init(&a_opa);
  lv_anim_set_var(&a_opa, card);
  lv_anim_set_values(&a_opa, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&a_opa, TOAST_ANIM_TIME_MS);
  lv_anim_set_exec_cb(&a_opa, anim_set_opa_cb);
  lv_anim_set_path_cb(&a_opa, anim_path_quadratic_ease_out);
  lv_anim_start(&a_opa);

  return root;
}

/* Toast 点击回调 */
static void _toast_click_cb(lv_event_t *e) {
  gs_toast_config_t *cfg = (gs_toast_config_t *)lv_event_get_user_data(e);
  if (cfg && cfg->click_cb) {
    cfg->click_cb(cfg->user_data);
  }
  gs_portal_toast_dismiss();
}

/* ========== Alert 视图创建 ========== */
static lv_obj_t *_alert_view_create(lv_obj_t *parent, gs_alert_config_t *cfg) {
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, LV_OPA_50, 0);
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(card, cfg->win_w > 0 ? cfg->win_w : 280,
                  cfg->win_h > 0 ? cfg->win_h : 160);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *title = lv_label_create(card);
  lv_obj_set_style_text_font(title, &chili_cn, 0);
  lv_label_set_text(title, cfg->title ? cfg->title : "");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *msg = lv_label_create(card);
  lv_obj_set_style_text_font(msg, &chili_cn, 0);
  lv_label_set_text(msg, cfg->msg ? cfg->msg : "");
  lv_obj_set_width(msg, lv_pct(100));
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *btn_cont = lv_obj_create(card);
  lv_obj_set_size(btn_cont, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_all(btn_cont, 0, 0);
  lv_obj_set_style_border_width(btn_cont, 0, 0);
  lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(btn_cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn_cont, 10, 0);

  lv_obj_t *cancel_btn = lv_button_create(btn_cont);
  lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x909399), 0);
  lv_obj_add_event_cb(cancel_btn, _alert_cancel_click, LV_EVENT_CLICKED, cfg);
  lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
  lv_obj_set_style_text_font(cancel_lbl, &chili_cn, 0);
  lv_label_set_text(cancel_lbl, "取消");

  lv_obj_t *ok_btn = lv_button_create(btn_cont);
  lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x2196f3), 0);
  lv_obj_add_event_cb(ok_btn, _alert_ok_click, LV_EVENT_CLICKED, cfg);
  lv_obj_t *ok_lbl = lv_label_create(ok_btn);
  lv_obj_set_style_text_font(ok_lbl, &chili_cn, 0);
  lv_label_set_text(ok_lbl, "确定");

  return root;
}

static void _alert_ok_click(lv_event_t *e) {
  gs_alert_config_t *cfg = (gs_alert_config_t *)lv_event_get_user_data(e);
  if (cfg && cfg->ok_cb)
    cfg->ok_cb(cfg->user_data);
  gs_portal_alert_dismiss();
}

static void _alert_cancel_click(lv_event_t *e) {
  gs_alert_config_t *cfg = (gs_alert_config_t *)lv_event_get_user_data(e);
  if (cfg && cfg->cancel_cb)
    cfg->cancel_cb(cfg->user_data);
  gs_portal_alert_dismiss();
}

/* ========== 自动关闭 Toast 定时器回调 ========== */
static void toast_timer_cb(TimerHandle_t xTimer) {
  (void)xTimer;
  // FreeRTOS Timer 线程中，安全发起 LVGL 异步任务
  lv_async_call(_async_dismiss_cb, (void *)0);
}

/* ========== 异步回调实现 ========== */
static void _async_create_cb(void *user_data) {
  portal_create_params_t *params = (portal_create_params_t *)user_data;
  if (!params)
    return;

  if (params->is_alert) {
    if (g_current_alert) {
      lv_obj_delete(g_current_alert);
      g_current_alert = NULL;
    }
    _free_alert_cfg(g_alert_cfg);

    g_alert_cfg = malloc(sizeof(gs_alert_config_t));
    memcpy(g_alert_cfg, &params->alert_cfg, sizeof(gs_alert_config_t));
    g_current_alert = _alert_view_create(lv_layer_top(), g_alert_cfg);
  } else {
    // 创建新 Toast 前强行注销并彻底清理旧 Toast
    if (g_current_toast) {
      lv_anim_del(g_current_toast, NULL);
      lv_obj_t *old_card = lv_obj_get_child(g_current_toast, 0);
      if (old_card)
        lv_anim_del(old_card, NULL);
      lv_obj_delete(g_current_toast);
      g_current_toast = NULL;
    }
    _free_toast_cfg(g_toast_cfg);

    g_toast_cfg = malloc(sizeof(gs_toast_config_t));
    memcpy(g_toast_cfg, &params->toast_cfg, sizeof(gs_toast_config_t));
    g_current_toast = _toast_view_create(lv_layer_top(), g_toast_cfg);

    if (g_toast_cfg->stay_time > 0) {
      if (g_toast_timer == NULL) {
        g_toast_timer =
            xTimerCreate("toast_tmr", pdMS_TO_TICKS(g_toast_cfg->stay_time),
                         pdFALSE, NULL, toast_timer_cb);
      } else {
        xTimerChangePeriod(g_toast_timer, pdMS_TO_TICKS(g_toast_cfg->stay_time),
                           0);
      }
      xTimerStart(g_toast_timer, 0);
    }
  }
  free(params);
}

static void _async_dismiss_cb(void *user_data) {
  bool is_alert = (bool)(intptr_t)user_data;
  if (is_alert) {
    if (g_current_alert) {
      lv_obj_delete(g_current_alert);
      g_current_alert = NULL;
    }
    _free_alert_cfg(g_alert_cfg);
    g_alert_cfg = NULL;
  } else {
    if (g_toast_timer) {
      xTimerStop(g_toast_timer, 0);
    }

    if (g_current_toast) {
      lv_obj_t *toast_to_dismiss = g_current_toast;
      g_current_toast = NULL; // 立即将全局指针置空，防止后续被误引用

      lv_anim_del(toast_to_dismiss, NULL);
      lv_obj_t *card = lv_obj_get_child(toast_to_dismiss, 0);

      if (card) {
        lv_anim_del(card, NULL);

        // 1. 上浮消失动画
        lv_anim_t a_pos;
        lv_anim_init(&a_pos);
        lv_anim_set_var(&a_pos, card);
        lv_anim_set_values(&a_pos, lv_obj_get_y(card), TOAST_TOP_START_Y);
        lv_anim_set_time(&a_pos, TOAST_ANIM_TIME_MS);
        lv_anim_set_exec_cb(&a_pos, anim_set_y_cb);
        lv_anim_set_path_cb(&a_pos, anim_path_quadratic_ease_in);
        lv_anim_start(&a_pos);

        // 2. 渐隐动画
        lv_anim_t a_opa;
        lv_anim_init(&a_opa);
        lv_anim_set_var(&a_opa, card);
        lv_anim_set_values(&a_opa, lv_obj_get_style_opa(card, 0),
                           LV_OPA_TRANSP);
        lv_anim_set_time(&a_opa, TOAST_ANIM_TIME_MS);
        lv_anim_set_exec_cb(&a_opa, anim_set_opa_cb);
        lv_anim_set_path_cb(&a_opa, anim_path_quadratic_ease_in);
        lv_anim_set_ready_cb(&a_opa, _toast_exit_anim_ready_cb);
        lv_anim_start(&a_opa);
      } else {
        lv_obj_delete(toast_to_dismiss);
      }
    }

    _free_toast_cfg(g_toast_cfg);
    g_toast_cfg = NULL;
  }
}

/* ========== 公开 API ========== */
void gs_portal_toast_show(gs_toast_config_t cfg) {
  portal_create_params_t *params = malloc(sizeof(portal_create_params_t));
  if (!params)
    return;
  params->is_alert = false;
  memcpy(&params->toast_cfg, &cfg, sizeof(gs_toast_config_t));

  // 深拷贝 msg 预防动态/局部字符串失效导致的悬空指针
  if (cfg.msg) {
    params->toast_cfg.msg = strdup(cfg.msg);
  }

  lv_async_call(_async_create_cb, params);
}

void gs_portal_toast_dismiss(void) {
  lv_async_call(_async_dismiss_cb, (void *)0);
}

void gs_portal_alert_show(gs_alert_config_t cfg) {
  portal_create_params_t *params = malloc(sizeof(portal_create_params_t));
  if (!params)
    return;
  params->is_alert = true;
  memcpy(&params->alert_cfg, &cfg, sizeof(gs_alert_config_t));

  // 深拷贝字符串
  if (cfg.title)
    params->alert_cfg.title = strdup(cfg.title);
  if (cfg.msg)
    params->alert_cfg.msg = strdup(cfg.msg);

  lv_async_call(_async_create_cb, params);
}

void gs_portal_alert_dismiss(void) {
  lv_async_call(_async_dismiss_cb, (void *)1);
}

void gs_toast_show(const char *msg, gs_toast_type_t type) {
  gs_toast_config_t cfg = GS_TOAST_DEFAULT_CONFIG();
  cfg.msg = msg;
  cfg.type = type;
  gs_portal_toast_show(cfg);
}

void gs_alert_show(const char *title, const char *msg) {
  gs_alert_config_t cfg = GS_ALERT_DEFAULT_CONFIG();
  cfg.title = title;
  cfg.msg = msg;
  gs_portal_alert_show(cfg);
}