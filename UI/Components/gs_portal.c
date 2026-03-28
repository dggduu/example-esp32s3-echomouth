#include "gs_portal.h"
#include "Styles/StyleSheet.h"
#include "easing.h"
#include "lvgl.h"
#include "stdlib.h"
#include <src/core/lv_obj_scroll.h>
#include <src/core/lv_obj_style.h>
#include <src/stdlib/lv_mem.h>
#include <string.h>

static struct {
  lv_obj_t *root;
  void (*update_cb)(lv_obj_t *root, int32_t progress);
  lv_timer_t *timer;
  uint32_t start_time;
  uint32_t total_duration;
  uint32_t stay_time;
  int32_t direction; /* 1 = 进入, -1 = 退出 */
  bool is_active;
  bool is_staying;
  bool auto_close;
} _fsm = {0};

void gs_portal_fsm_dismiss(uint32_t duration);

static gs_alert_config_t *g_alert_cfg = NULL;
static gs_toast_config_t *g_toast_cfg = NULL;

static void _portal_fsm_timer_cb(lv_timer_t *t) {
  if (!_fsm.root)
    return;

  if (_fsm.is_staying) {
    if (lv_tick_elaps(_fsm.start_time) >= _fsm.stay_time) {
      _fsm.is_staying = false;
      uint32_t out_dur = (_fsm.total_duration > 0) ? _fsm.total_duration
                                                   : PORTAL_ANIM_DURATION_OUT;
      gs_portal_fsm_dismiss(out_dur);
    }
    return;
  }

  if (!_fsm.is_active)
    return;

  uint32_t elapsed = lv_tick_elaps(_fsm.start_time);
  float t_norm = (float)elapsed / (float)_fsm.total_duration;
  if (t_norm >= 1.0f) {
    t_norm = 1.0f;
    _fsm.is_active = false;
    if (_fsm.direction == 1 && _fsm.auto_close) {
      _fsm.is_staying = true;
      _fsm.start_time = lv_tick_get();
      return;
    }
  }

  float p;
  if (_fsm.direction == 1) {
    p = PORTAL_ANIM_IN_EASE(t_norm);
  } else {
    float ease_val = PORTAL_ANIM_OUT_EASE(t_norm);
    p = 1.0f - ease_val;
  }

  if (_fsm.update_cb)
    _fsm.update_cb(_fsm.root, (int32_t)(p * 100.0f));

  if (!_fsm.is_active && _fsm.direction == -1) {
    lv_obj_delete(_fsm.root);
    _fsm.root = NULL;
    lv_timer_pause(_fsm.timer);
  }
}

static void _portal_fsm_start(lv_obj_t *(*render_fn)(lv_obj_t *parent),
                              void (*update_cb)(lv_obj_t *root,
                                                int32_t progress),
                              uint32_t anim_in_dur, uint32_t anim_out_dur,
                              bool auto_close, uint32_t stay_time) {
  if (_fsm.root)
    lv_obj_delete(_fsm.root);

  _fsm.root = render_fn(lv_layer_top());
  _fsm.update_cb = update_cb;
  _fsm.total_duration =
      (anim_in_dur > 0) ? anim_in_dur : PORTAL_ANIM_DURATION_IN;
  _fsm.stay_time = stay_time;
  _fsm.auto_close = auto_close;
  _fsm.start_time = lv_tick_get();
  _fsm.direction = 1;
  _fsm.is_active = true;
  _fsm.is_staying = false;

  if (!_fsm.timer)
    _fsm.timer = lv_timer_create(_portal_fsm_timer_cb, 16, NULL);
  else
    lv_timer_resume(_fsm.timer);

  if (_fsm.update_cb)
    _fsm.update_cb(_fsm.root, 0);
}

void gs_portal_fsm_dismiss(uint32_t duration) {
  if (!_fsm.root)
    return;
  _fsm.total_duration = (duration > 0) ? duration : PORTAL_ANIM_DURATION_OUT;
  _fsm.start_time = lv_tick_get();
  _fsm.direction = -1;
  _fsm.is_active = true;
  _fsm.is_staying = false;
  if (_fsm.timer)
    lv_timer_resume(_fsm.timer);
}

/* ---------- Alert 视图 ---------- */
static int32_t _alert_start_y, _alert_end_y;

static void _alert_render_step(lv_obj_t *root, int32_t progress) {
  lv_obj_t *card = lv_obj_get_child(root, 0);
  if (!card)
    return;
  int32_t cur_y =
      _alert_start_y + ((_alert_end_y - _alert_start_y) * progress) / 100;
  lv_obj_set_y(card, cur_y);
}

static void _alert_ok_click(lv_event_t *e) {
  gs_alert_config_t *cfg = (gs_alert_config_t *)lv_event_get_user_data(e);
  if (cfg->ok_cb)
    cfg->ok_cb(cfg->user_data);
  uint32_t out_dur =
      (cfg->anim_out_time > 0) ? cfg->anim_out_time : PORTAL_ANIM_DURATION_OUT;
  gs_portal_fsm_dismiss(out_dur);
}

static void _alert_cancel_click(lv_event_t *e) {
  gs_alert_config_t *cfg = (gs_alert_config_t *)lv_event_get_user_data(e);
  if (cfg->cancel_cb)
    cfg->cancel_cb(cfg->user_data);
  uint32_t out_dur =
      (cfg->anim_out_time > 0) ? cfg->anim_out_time : PORTAL_ANIM_DURATION_OUT;
  gs_portal_fsm_dismiss(out_dur);
}

static lv_obj_t *_alert_base_render(lv_obj_t *parent, bool modal) {
  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, 0, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  if (!modal)
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
  return root;
}

static lv_obj_t *_alert_view(lv_obj_t *parent, gs_alert_config_t *cfg) {
  lv_obj_t *root = _alert_base_render(parent, true);
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(card, cfg->win_w > 0 ? cfg->win_w : 280,
                  cfg->win_h > 0 ? cfg->win_h : 160);
  lv_obj_set_style_radius(card, EL_RADIUS_BORDER_BASE, 0);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

  _alert_end_y = lv_obj_get_y(card);
  _alert_start_y = _alert_end_y - 40;

  /* 标题 */
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, cfg->title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 5);

  /* 消息内容 */
  lv_obj_t *msg = lv_label_create(card);
  lv_label_set_text(msg, cfg->msg);
  lv_obj_set_width(msg, lv_pct(100));
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_flag(msg, LV_OBJ_FLAG_SCROLLABLE, false);

  /* 按钮容器 */
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
  lv_obj_set_style_bg_color(cancel_btn, EL_COLOR_INFO, 0);
  lv_obj_add_event_cb(cancel_btn, _alert_cancel_click, LV_EVENT_CLICKED, cfg);
  lv_label_set_text(lv_label_create(cancel_btn), "Cancel");

  lv_obj_t *ok_btn = lv_button_create(btn_cont);
  lv_obj_set_style_bg_color(ok_btn, EL_COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(ok_btn, _alert_ok_click, LV_EVENT_CLICKED, cfg);
  lv_label_set_text(lv_label_create(ok_btn), "OK");

  return root;
}

/* ---------- Toast 视图 ---------- */
static int32_t _toast_start_y, _toast_end_y;

static void _toast_render_step(lv_obj_t *root, int32_t progress) {
  lv_obj_t *card = lv_obj_get_child(root, 0);
  if (!card)
    return;
  int32_t cur_y =
      _toast_start_y + ((_toast_end_y - _toast_start_y) * progress) / 100;
  lv_obj_set_y(card, cur_y);
}

static void _toast_click_cb(lv_event_t *e) {
  gs_toast_config_t *cfg = (gs_toast_config_t *)lv_event_get_user_data(e);
  if (cfg->click_cb)
    cfg->click_cb(cfg->user_data);
  uint32_t out_dur =
      (cfg->anim_out_time > 0) ? cfg->anim_out_time : PORTAL_ANIM_DURATION_OUT;
  gs_portal_fsm_dismiss(out_dur);
}

static lv_obj_t *_toast_view(lv_obj_t *parent, gs_toast_config_t *cfg) {
  lv_obj_t *root = _alert_base_render(parent, false); /* 无模态遮罩 */
  lv_obj_t *card = lv_obj_create(root);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

  lv_obj_set_size(
      card, (float)lv_display_get_horizontal_resolution(NULL) / 2 + 20, 30);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
  lv_obj_set_style_text_color(card, lv_color_white(), 0);

  /* 根据类型设置背景色 */
  lv_color_t bg_color;
  switch (cfg->type) {
  case GS_TOAST_SUCCESS:
    bg_color = lv_color_hex(0x529b2e); /* 绿色 */
    break;
  case GS_TOAST_FAILED:
    bg_color = lv_color_hex(0xc45656); /* 红色 */
    break;
  case GS_TOAST_INFO:
  default:
    bg_color = lv_color_hex(0x909399); /* 深灰色 */
    break;
  }
  lv_obj_set_style_bg_color(card, bg_color, 0);

  lv_obj_t *msg = lv_label_create(card);
  lv_label_set_text(msg, cfg->msg);
  lv_obj_center(msg);

  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);

  _toast_end_y = lv_obj_get_y(card) + 10; // top padding
  _toast_start_y = -lv_obj_get_height(card) - 10;

  /* 添加点击回调 */
  if (cfg->click_cb) {
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, _toast_click_cb, LV_EVENT_CLICKED, cfg);
  }

  return root;
}

/* Alert 渲染函数 */
static lv_obj_t *_alert_render(lv_obj_t *parent) {
  return _alert_view(parent, g_alert_cfg);
}

/* Toast 渲染函数 */
static lv_obj_t *_toast_render(lv_obj_t *parent) {
  return _toast_view(parent, g_toast_cfg);
}

/* ---------- 公开 API ---------- */
void gs_portal_alert_show(gs_alert_config_t cfg) {
  gs_alert_config_t *cfg_copy = malloc(sizeof(gs_alert_config_t));
  if (!cfg_copy)
    return;
  memcpy(cfg_copy, &cfg, sizeof(gs_alert_config_t));
  g_alert_cfg = cfg_copy;

  uint32_t anim_in =
      (cfg.anim_in_time > 0) ? cfg.anim_in_time : PORTAL_ANIM_DURATION_IN;
  uint32_t anim_out =
      (cfg.anim_out_time > 0) ? cfg.anim_out_time : PORTAL_ANIM_DURATION_OUT;
  _portal_fsm_start(_alert_render, _alert_render_step, anim_in, anim_out, false,
                    0);
}

void gs_portal_toast_show(gs_toast_config_t cfg) {
  gs_toast_config_t *cfg_copy = malloc(sizeof(gs_toast_config_t));
  if (!cfg_copy)
    return;
  memcpy(cfg_copy, &cfg, sizeof(gs_toast_config_t));
  g_toast_cfg = cfg_copy;

  uint32_t anim_in =
      (cfg.anim_in_time > 0) ? cfg.anim_in_time : PORTAL_ANIM_DURATION_IN;
  uint32_t stay =
      (cfg.stay_time > 0) ? cfg.stay_time : PORTAL_TOAST_STAY_DEFAULT;
  _portal_fsm_start(_toast_render, _toast_render_step, anim_in, 0, true, stay);
}