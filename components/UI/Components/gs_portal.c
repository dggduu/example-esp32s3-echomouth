#include "gs_portal.h"
#include "easing.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PORTAL";

/* ========== 1. 字体声明 ========== */
LV_FONT_DECLARE(chili_cn);

/* ========== 内部状态变量 ========== */
static lv_obj_t *g_current_toast = NULL;
static lv_obj_t *g_current_alert = NULL;

// 使用 LVGL 原生 Timer 代替 FreeRTOS Timer，彻底解决多线程/清除失败问题
static lv_timer_t *g_toast_lv_timer = NULL;

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
#define TOAST_TOP_TARGET_Y 20 // 针对 360 圆形屏优化顶部边距
/* 等待 LVGL 锁的预算：启动阶段 splash 渲染 / SPI 大区域刷新可能超过 100ms */
#define LVGL_LOCK_TIMEOUT_MS 250
/* 起始 Y 不能完全移出屏幕：lv_inv_area 会丢弃完全在屏幕外的失效区域，
 * 若动画因任何原因被延迟/卡住，卡在屏外的 toast 将完全不可见且不会被重绘
 * （"触发但没有下移" 且毫无痕迹）。-30 保证卡片底部始终有一截在屏内。 */
#define TOAST_TOP_START_Y -30

/* ========== 内部函数前置声明 ========== */
static void _toast_click_cb(lv_event_t *e);
static void _alert_ok_click(lv_event_t *e);
static void _alert_cancel_click(lv_event_t *e);
static void _async_create_cb(void *user_data);
static void _async_dismiss_cb(void *user_data);
static void _toast_timer_expire_cb(lv_timer_t *timer);

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

/* ========== 缓动 Path 回调函数 ========== */
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

/* Toast 消失动画完成回调 */
static void _toast_exit_anim_ready_cb(lv_anim_t *a) {
  lv_obj_t *root = (lv_obj_t *)a->var;
  if (lv_obj_is_valid(root)) {
    if (root == g_current_toast) {
      g_current_toast = NULL;
    }
    lv_obj_delete(root);
  }
}

/* ========== Toast 视图创建（适配 360x360 圆形屏） ========== */
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
  lv_obj_set_style_radius(card, 16, 0); // 稍微加大圆角，配合圆形屏
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x444444), 0);
  lv_obj_set_style_pad_left(card, 12, 0);
  lv_obj_set_style_pad_right(card, 12, 0);
  lv_obj_set_style_pad_top(card, 8, 0);
  lv_obj_set_style_pad_bottom(card, 8, 0);

  // 背景色与文字颜色
  lv_color_t bg_color;
  lv_color_t text_color = lv_color_hex(0x333333);

  switch (cfg->type) {
  case GS_TOAST_SUCCESS:
    bg_color = lv_color_hex(0xe1f3d8);
    text_color = lv_color_hex(0x529b2e);
    break;
  case GS_TOAST_FAILED:
    bg_color = lv_color_hex(0xfcd3d3);
    text_color = lv_color_hex(0xc45656);
    break;
  default:
    bg_color = lv_color_hex(0xe9e9eb);
    text_color = lv_color_hex(0x606266);
    break;
  }
  lv_obj_set_style_bg_color(card, bg_color, 0);
  lv_obj_set_style_text_color(card, text_color, 0);

  /* UTF-8 字符计数 */
  int char_count = 0;
  {
    const char *p = cfg->msg ? cfg->msg : "";
    while (*p) {
      if ((*p & 0xC0) != 0x80)
        char_count++; // UTF-8 多字节字符计数
      p++;
    }
  }

  /* 360x360 圆形屏适配逻辑：
   * 顶部区域（Y=15~60）最大宽度不能超过 210px，否则左右两端会被圆屏切掉！
   */
  const lv_coord_t max_circle_w = 200;

  lv_obj_t *label = lv_label_create(card);
  lv_obj_set_style_text_font(label, &chili_cn, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label, cfg->msg ? cfg->msg : "");

  if (char_count <= 5) {
    /* 需求：<= 5 个字符，宽度自适应 */
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_height(label, LV_SIZE_CONTENT);

    lv_obj_set_width(card, LV_SIZE_CONTENT);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
  } else {
    /* 需求：> 5 个字符，固定 Toast 宽度，高度自动随换行增高 */
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, max_circle_w - 24); // 扣除 padding

    lv_obj_set_width(card, max_circle_w);
    lv_obj_set_height(card, LV_SIZE_CONTENT); // 高度随文字行数自适应
  }

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
  // 360 圆屏中央居中对话框，宽度不宜超过 260px
  lv_obj_set_size(card, cfg->win_w > 0 ? cfg->win_w : 240,
                  cfg->win_h > 0 ? cfg->win_h : 160);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *title = lv_label_create(card);
  lv_obj_set_style_text_font(title, &chili_cn, 0);
  lv_label_set_text(title, cfg->title ? cfg->title : "");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *msg = lv_label_create(card);
  lv_obj_set_style_text_font(msg, &chili_cn, 0);
  lv_label_set_text(msg, cfg->msg ? cfg->msg : "");
  lv_obj_set_width(msg, lv_pct(100));
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(msg, LV_ALIGN_CENTER, 0, -5);

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
  lv_obj_set_style_pad_column(btn_cont, 12, 0);

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

/* ========== LVGL Timer 到期自动消失回调 ========== */
static void _toast_timer_expire_cb(lv_timer_t *timer) {
  (void)timer;
  g_toast_lv_timer = NULL; // 定时器一次性触发后会自动删除，在此将全局指针清空
  _async_dismiss_cb((void *)0);
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
    // 1. 如果已有定时器，立即停止并删除，防止触发旧 Toast 的清除
    if (g_toast_lv_timer) {
      lv_timer_delete(g_toast_lv_timer);
      g_toast_lv_timer = NULL;
    }

    // 2. 如果当前已有 Toast，停止一切关联动画并强行删除对象
    if (g_current_toast) {
      lv_anim_del(g_current_toast, NULL);
      lv_obj_t *old_card = lv_obj_get_child(g_current_toast, 0);
      if (old_card) {
        lv_anim_del(old_card, NULL);
      }
      lv_obj_delete(g_current_toast);
      g_current_toast = NULL;
    }
    _free_toast_cfg(g_toast_cfg);

    // 3. 创建新 Toast
    g_toast_cfg = malloc(sizeof(gs_toast_config_t));
    memcpy(g_toast_cfg, &params->toast_cfg, sizeof(gs_toast_config_t));
    g_current_toast = _toast_view_create(lv_layer_top(), g_toast_cfg);

    // 4. 使用 LVGL 原生单次 Timer 启动倒计时
    if (g_toast_cfg->stay_time > 0) {
      g_toast_lv_timer =
          lv_timer_create(_toast_timer_expire_cb, g_toast_cfg->stay_time, NULL);
      lv_timer_set_repeat_count(g_toast_lv_timer, 1);
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
    // 安全注销 LVGL 定时器
    if (g_toast_lv_timer) {
      lv_timer_delete(g_toast_lv_timer);
      g_toast_lv_timer = NULL;
    }

    if (g_current_toast) {
      lv_obj_t *toast_to_dismiss = g_current_toast;
      g_current_toast = NULL; // 立即将指针置空，防止重入/误引用

      lv_anim_del(toast_to_dismiss, NULL);
      lv_obj_t *card = lv_obj_get_child(toast_to_dismiss, 0);

      if (card) {
        lv_anim_del(card, NULL);

        /* 卡片即将销毁：其 click 事件 user_data 指向 g_toast_cfg，
         * 稍后 _free_toast_cfg 会释放该配置 —— 退场动画 300ms 内点击卡片
         * 会命中已释放内存（UAF）。先摘除点击回调，封死该窗口。 */
        lv_obj_remove_event_cb(card, _toast_click_cb);

        // 1. 上浮退场动画
        lv_anim_t a_pos;
        lv_anim_init(&a_pos);
        lv_anim_set_var(&a_pos, card);
        lv_anim_set_values(&a_pos, lv_obj_get_y(card), TOAST_TOP_START_Y);
        lv_anim_set_time(&a_pos, TOAST_ANIM_TIME_MS);
        lv_anim_set_exec_cb(&a_pos, anim_set_y_cb);
        lv_anim_set_path_cb(&a_pos, anim_path_quadratic_ease_in);
        lv_anim_start(&a_pos);

        /* 2. 渐隐动画：var 直接绑 root（root 的 opa 作用于整棵子树），
         * 动画结束由 ready 回调删除 root。 */
        lv_anim_t a_opa;
        lv_anim_init(&a_opa);
        lv_anim_set_var(&a_opa, toast_to_dismiss);
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
/* lv_async_call 本质是 lv_timer_create —— 无锁向 LVGL 全局定时器链表头
 * 插入一次性定时器。外部任务（net_rx、上传任务、websocket 事件等）直接
 * 调用时若与 taskLVGL 的 lv_timer_handler 并发迭代/删除同一链表，定时器
 * 可能丢失或链表损坏（弹窗"触发但不显示"）。必须在 LVGL 锁保护下插入；
 * taskLVGL 内部（动画/事件回调）调用时递归锁可重入，安全。 */
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

  if (!lvgl_port_lock(pdMS_TO_TICKS(LVGL_LOCK_TIMEOUT_MS))) {
    ESP_LOGE(TAG, "toast show: LVGL lock timeout, drop");
    /* 注意：不能用 _free_toast_cfg(&params->toast_cfg) —— 它会最后
     * free(cfg) 自身，而 toast_cfg 只是 params 内部的 union 成员（非独立
     * malloc 基址），free 内部指针会被判为堆损坏直接 panic。
     * 这里只释放 strdup 的字段，再 free(params) 基址。 */
    if (params->toast_cfg.msg)
      free((void *)params->toast_cfg.msg);
    free(params);
    return;
  }
  if (lv_async_call(_async_create_cb, params) != LV_RESULT_OK) {
    if (params->toast_cfg.msg)
      free((void *)params->toast_cfg.msg);
    free(params);
  }
  lvgl_port_unlock();
}

void gs_portal_toast_dismiss(void) {
  if (!lvgl_port_lock(pdMS_TO_TICKS(LVGL_LOCK_TIMEOUT_MS))) {
    ESP_LOGE(TAG, "toast dismiss: LVGL lock timeout");
    return;
  }
  lv_async_call(_async_dismiss_cb, (void *)0);
  lvgl_port_unlock();
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

  if (!lvgl_port_lock(pdMS_TO_TICKS(LVGL_LOCK_TIMEOUT_MS))) {
    ESP_LOGE(TAG, "alert show: LVGL lock timeout, drop");
    /* 同 toast：alert_cfg 是 params 内部成员，不能整个 _free_alert_cfg */
    if (params->alert_cfg.title)
      free((void *)params->alert_cfg.title);
    if (params->alert_cfg.msg)
      free((void *)params->alert_cfg.msg);
    free(params);
    return;
  }
  if (lv_async_call(_async_create_cb, params) != LV_RESULT_OK) {
    if (params->alert_cfg.title)
      free((void *)params->alert_cfg.title);
    if (params->alert_cfg.msg)
      free((void *)params->alert_cfg.msg);
    free(params);
  }
  lvgl_port_unlock();
}

void gs_portal_alert_dismiss(void) {
  if (!lvgl_port_lock(pdMS_TO_TICKS(LVGL_LOCK_TIMEOUT_MS))) {
    ESP_LOGE(TAG, "alert dismiss: LVGL lock timeout");
    return;
  }
  lv_async_call(_async_dismiss_cb, (void *)1);
  lvgl_port_unlock();
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