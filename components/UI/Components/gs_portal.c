#include "gs_portal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lvgl.h"
#include "stdlib.h"
#include "string.h"

/* ========== 全局当前显示的 Toast/Alert 对象 ========== */
static lv_obj_t *g_current_toast = NULL;
static lv_obj_t *g_current_alert = NULL;
static TimerHandle_t g_toast_timer = NULL;

/* 配置副本，用于回调中访问 */
static gs_toast_config_t *g_toast_cfg = NULL;
static gs_alert_config_t *g_alert_cfg = NULL;

/* ========== 异步任务参数结构 ========== */
typedef struct {
  bool is_alert;
  union {
    gs_toast_config_t toast_cfg;
    gs_alert_config_t alert_cfg;
  };
} portal_create_params_t;

/* ========== 内部函数前置声明 ========== */
static void _toast_click_cb(lv_event_t *e);
static void _alert_ok_click(lv_event_t *e);
static void _alert_cancel_click(lv_event_t *e);
static void _async_create_cb(void *user_data);
static void _async_dismiss_cb(void *user_data);
static void toast_timer_cb(TimerHandle_t xTimer);

/* ========== Toast 视图创建 ========== */
static lv_obj_t *_toast_view_create(lv_obj_t *parent, gs_toast_config_t *cfg) {
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, 0, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(card, lv_display_get_horizontal_resolution(NULL) * 2 / 3, 36);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
  lv_obj_set_style_text_color(card, lv_color_white(), 0);

  lv_color_t bg_color;
  switch (cfg->type) {
  case GS_TOAST_SUCCESS:
    bg_color = lv_color_hex(0x529b2e);
    break;
  case GS_TOAST_FAILED:
    bg_color = lv_color_hex(0xc45656);
    break;
  default:
    bg_color = lv_color_hex(0x909399);
    break;
  }
  lv_obj_set_style_bg_color(card, bg_color, 0);

  lv_obj_t *label = lv_label_create(card);
  lv_label_set_text(label, cfg->msg);
  lv_obj_center(label);

  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 10);

  if (cfg->click_cb) {
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, _toast_click_cb, LV_EVENT_CLICKED, cfg);
  }

  return root;
}

/* Toast 点击回调 */
static void _toast_click_cb(lv_event_t *e) {
  gs_toast_config_t *cfg = (gs_toast_config_t *)lv_event_get_user_data(e);
  if (cfg->click_cb) {
    cfg->click_cb(cfg->user_data);
  }
  gs_portal_toast_dismiss();
}

/* ========== Alert 视图创建 ========== */
static lv_obj_t *_alert_view_create(lv_obj_t *parent, gs_alert_config_t *cfg) {
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, 0, 0);
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
  lv_label_set_text(title, cfg->title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 5);

  lv_obj_t *msg = lv_label_create(card);
  lv_label_set_text(msg, cfg->msg);
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
  lv_label_set_text(lv_label_create(cancel_btn), "Cancel");

  lv_obj_t *ok_btn = lv_button_create(btn_cont);
  lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x2196f3), 0);
  lv_obj_add_event_cb(ok_btn, _alert_ok_click, LV_EVENT_CLICKED, cfg);
  lv_label_set_text(lv_label_create(ok_btn), "OK");

  return root;
}

static void _alert_ok_click(lv_event_t *e) {
  gs_alert_config_t *cfg = (gs_alert_config_t *)lv_event_get_user_data(e);
  if (cfg->ok_cb)
    cfg->ok_cb(cfg->user_data);
  gs_portal_alert_dismiss();
}

static void _alert_cancel_click(lv_event_t *e) {
  gs_alert_config_t *cfg = (gs_alert_config_t *)lv_event_get_user_data(e);
  if (cfg->cancel_cb)
    cfg->cancel_cb(cfg->user_data);
  gs_portal_alert_dismiss();
}

/* ========== 定时器回调：自动关闭 Toast ========== */
static void toast_timer_cb(TimerHandle_t xTimer) { gs_portal_toast_dismiss(); }

/* ========== 异步回调实现 ========== */
static void _async_create_cb(void *user_data) {
  portal_create_params_t *params = (portal_create_params_t *)user_data;
  if (!params)
    return;

  if (params->is_alert) {
    if (g_current_alert)
      lv_obj_delete(g_current_alert);
    g_alert_cfg = malloc(sizeof(gs_alert_config_t));
    memcpy(g_alert_cfg, &params->alert_cfg, sizeof(gs_alert_config_t));
    g_current_alert = _alert_view_create(lv_layer_top(), g_alert_cfg);
  } else {
    if (g_current_toast)
      lv_obj_delete(g_current_toast);
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
    if (g_alert_cfg) {
      free(g_alert_cfg);
      g_alert_cfg = NULL;
    }
  } else {
    if (g_toast_timer) {
      xTimerStop(g_toast_timer, 0);
    }
    if (g_current_toast) {
      lv_obj_delete(g_current_toast);
      g_current_toast = NULL;
    }
    if (g_toast_cfg) {
      free(g_toast_cfg);
      g_toast_cfg = NULL;
    }
  }
}

/* ========== 公开 API ========== */
void gs_portal_toast_show(gs_toast_config_t cfg) {
  portal_create_params_t *params = malloc(sizeof(portal_create_params_t));
  if (!params)
    return;
  params->is_alert = false;
  memcpy(&params->toast_cfg, &cfg, sizeof(gs_toast_config_t));
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