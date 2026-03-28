#include "watch_lock_nav.h"

typedef struct {
  gs_view_cb main_hook;
  unlock_config_t config;
  lv_obj_t *lock_screen_obj;
} lock_ctx_t;

static lock_ctx_t s_ctx = {0};

/* 解锁执行函数 */
static void execute_unlock(lock_ctx_t *ctx) {
  if (!ctx->lock_screen_obj)
    return;

  // 创建主界面
  if (ctx->main_hook) {
    lv_obj_t *main_page = ctx->main_hook(lv_screen_active());
    if (main_page) {
      lv_obj_set_size(main_page, LV_PCT(100), LV_PCT(100));
      lv_obj_move_background(main_page);
    } else {
      // 主界面创建失败...
      return;
    }
  }

  // 异步销毁锁屏层
  lv_obj_del_async(ctx->lock_screen_obj);
  ctx->lock_screen_obj = NULL;
}

/* 事件回调 */
static void lock_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lock_ctx_t *ctx = lv_event_get_user_data(e);

  if (code == LV_EVENT_GESTURE) {
    lv_dir_t gesture = lv_indev_get_gesture_dir(lv_indev_active());
    if (ctx->config.dir_mask & gesture) {
      execute_unlock(ctx);
    }
  } else if (code == LV_EVENT_CLICKED && ctx->config.click_unlock) {
    execute_unlock(ctx);
  }
}

int watch_lock_nav_start(gs_view_cb lock_hook, gs_view_cb main_hook,
                         const unlock_config_t *config) {
  if (!lock_hook || !main_hook || !config)
    return -1;

  // 保存配置
  s_ctx.main_hook = main_hook;
  s_ctx.config = *config;

  // 清除已有锁屏
  if (s_ctx.lock_screen_obj) {
    lv_obj_del(s_ctx.lock_screen_obj);
    s_ctx.lock_screen_obj = NULL;
  }

  // 创建锁屏背景层
  lv_obj_t *lock = lv_obj_create(lv_screen_active());
  if (!lock)
    return -1;

  lv_obj_set_size(lock, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(lock, lv_color_black(), 0);
  lv_obj_set_style_border_width(lock, 0, 0);
  lv_obj_set_style_radius(lock, 0, 0);
  lv_obj_add_flag(lock, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(lock, LV_OBJ_FLAG_GESTURE_BUBBLE);

  // 添加事件
  lv_obj_add_event_cb(lock, lock_event_cb, LV_EVENT_ALL, &s_ctx);

  // 调用用户回调填充锁屏内容
  lock_hook(lock);

  s_ctx.lock_screen_obj = lock;
  return 0;
}