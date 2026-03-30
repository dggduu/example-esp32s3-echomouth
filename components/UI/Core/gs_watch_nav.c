#include "esp_log.h"
#include "watch_lock_nav.h"


static const char *TAG = "watch_nav";

typedef struct {
  gs_view_cb main_hook;
  unlock_config_t config;
  lv_obj_t *lock_screen_obj;
  lv_obj_t *main_screen_obj;
} lock_ctx_t;

static lock_ctx_t s_ctx = {0};

static void anim_ready_cb(lv_anim_t *a) {
  lv_obj_t *obj = (lv_obj_t *)lv_anim_get_user_data(a);
  if (obj)
    lv_obj_del(obj);
  s_ctx.lock_screen_obj = NULL;
}

static void execute_unlock_animation(lock_ctx_t *ctx) {
  if (!ctx->lock_screen_obj)
    return;

  lv_obj_remove_flag(ctx->lock_screen_obj, LV_OBJ_FLAG_CLICKABLE);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ctx->lock_screen_obj);
  // LVGL 9 使用 lv_display_get_vertical_resolution(NULL)
  int32_t screen_h = lv_display_get_vertical_resolution(NULL);
  lv_anim_set_values(&a, 0, -screen_h);
  lv_anim_set_duration(&a, 400); // 9.x 中使用 duration
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&a, anim_ready_cb); // 9.x 建议用 completed_cb
  lv_anim_set_user_data(&a, ctx->lock_screen_obj);
  lv_anim_start(&a);
}

static void lock_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_GESTURE) {
    lv_dir_t gesture = lv_indev_get_gesture_dir(lv_indev_get_act());
    ESP_LOGI(TAG, "Gesture Triggered: %d", (int)gesture);

    // 向上滑动的动作 (手指往上提)
    if (gesture == LV_DIR_TOP) {
      execute_unlock_animation(&s_ctx);
    }
  }
}

int watch_lock_nav_start(gs_view_cb lock_hook, gs_view_cb main_hook,
                         const unlock_config_t *config) {
  if (!lock_hook || !main_hook || !config)
    return -1;

  // 1. 底层主界面
  if (s_ctx.main_screen_obj)
    lv_obj_del(s_ctx.main_screen_obj);
  s_ctx.main_screen_obj = main_hook(lv_screen_active());
  lv_obj_set_size(s_ctx.main_screen_obj, LV_PCT(100), LV_PCT(100));

  // 2. 顶层锁屏容器
  if (s_ctx.lock_screen_obj)
    lv_obj_del(s_ctx.lock_screen_obj);

  // 创建一个全屏的基础容器
  lv_obj_t *lock = lv_obj_create(lv_screen_active());
  s_ctx.lock_screen_obj = lock;
  s_ctx.config = *config;

  lv_obj_set_size(lock, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(lock, lv_color_hex(0x000000), 0); // 黑色背景
  lv_obj_set_style_border_width(lock, 0, 0);
  lv_obj_set_style_radius(lock, 0, 0);
  lv_obj_move_foreground(lock);

  // --- LVGL 9 核心修复项 ---
  lv_obj_remove_flag(lock, LV_OBJ_FLAG_SCROLLABLE);  // 必须禁止滚动
  lv_obj_add_flag(lock, LV_OBJ_FLAG_CLICKABLE);      // 必须允许点击
  lv_obj_add_flag(lock, LV_OBJ_FLAG_GESTURE_BUBBLE); // 子控件手势向上传递

  // 强制增加触摸感应区域，防止边缘滑动失效
  lv_obj_set_ext_click_area(lock, 20);

  lv_obj_add_event_cb(lock, lock_event_cb, LV_EVENT_GESTURE, &s_ctx);

  // 填充用户 UI
  lock_hook(lock);

  return 0;
}