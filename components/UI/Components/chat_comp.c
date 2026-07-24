#include "chat_comp.h"
#include "StyleSheet.h"
#include "chat_service.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "lvgl.h"
#include "ui_circle_toolkit.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "CHAT_COMP";

static lv_obj_t *s_bubbles[CHAT_WINDOW_SIZE];
static lv_obj_t *s_labels[CHAT_WINDOW_SIZE];
static lv_obj_t *s_wrappers[CHAT_WINDOW_SIZE];
static lv_obj_t *s_textarea;
static lv_obj_t *s_root;
static lv_obj_t *s_chat_viewport;
static lv_obj_t *s_pinyin_ime = NULL;
static lv_obj_t *s_kb = NULL;
static lv_timer_t *s_poll_timer = NULL;

LV_FONT_DECLARE(chili_cn);

static lv_obj_t *create_fab_btn(lv_obj_t *parent, const char *icon,
                                lv_color_t bg_col, lv_color_t text_col);

static void render_window(void) {
  char time_buf[16];
  for (int i = 0; i < CHAT_WINDOW_SIZE; i++) {
    msg_t *m = chat_fifo_get(i);
    if (!m) {
      lv_obj_add_flag(s_wrappers[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(s_wrappers[i], LV_OBJ_FLAG_HIDDEN);

    time_t raw = (time_t)m->timestamp;
    struct tm *tm_info = localtime(&raw);
    if (tm_info && m->timestamp != 0) {
      strftime(time_buf, sizeof(time_buf), "%H:%M", tm_info);
    } else {
      strcpy(time_buf, "Now");
    }

    uint32_t color_val = (m->sender == 1) ? 0x21005D : 0x49454F;
    lv_label_set_text_fmt(s_labels[i], "#%" PRIx32 " %s#\n%s", color_val,
                          time_buf, m->text);

    if (m->sender == 1) {
      lv_obj_set_style_bg_color(s_bubbles[i], S_COLOR_PRIMARY_CONTAINER, 0);
      lv_obj_set_style_text_color(s_labels[i], S_COLOR_ON_PRIMARY_CONTAINER, 0);
      lv_obj_set_flex_align(s_wrappers[i], LV_FLEX_ALIGN_END,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
      lv_obj_set_style_bg_color(s_bubbles[i], S_COLOR_SURFACE_CONTAINER_HIGH,
                                0);
      lv_obj_set_style_text_color(s_labels[i], S_COLOR_ON_SURFACE, 0);
      lv_obj_set_flex_align(s_wrappers[i], LV_FLEX_ALIGN_START,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
  }
  lv_obj_update_layout(s_chat_viewport);
  lv_obj_scroll_to_view(s_wrappers[CHAT_WINDOW_SIZE - 1], LV_ANIM_OFF);
}

static void chat_poll_timer_cb(lv_timer_t *timer) {
  if (chat_window_is_dirty()) {
    chat_window_clear_dirty();
    render_window();
  }
}

static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target_obj(e);
  lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);

  if (code == LV_EVENT_FOCUSED) {
    if (lv_indev_get_type(lv_indev_active()) != LV_INDEV_TYPE_KEYPAD) {
      if (s_pinyin_ime) {
        lv_ime_pinyin_set_mode(s_pinyin_ime, LV_IME_PINYIN_MODE_K9);
      }

      lv_keyboard_set_textarea(kb, ta);
      lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);

      if (s_pinyin_ime) {
        lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(s_pinyin_ime);
        if (cand) {
          lv_obj_remove_flag(cand, LV_OBJ_FLAG_HIDDEN);
          lv_obj_align_to(cand, kb, LV_ALIGN_OUT_TOP_MID, 0, -8);
        }
      }

      lv_obj_set_height(s_chat_viewport, 105);
      lv_obj_scroll_to_view(s_wrappers[CHAT_WINDOW_SIZE - 1], LV_ANIM_OFF);
    }
  } else if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    if (s_pinyin_ime) {
      lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(s_pinyin_ime);
      if (cand)
        lv_obj_add_flag(cand, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    lv_indev_reset(NULL, ta);

    lv_obj_set_height(s_chat_viewport, 205);
    lv_obj_scroll_to_view(s_wrappers[CHAT_WINDOW_SIZE - 1], LV_ANIM_OFF);
  }
}

static void send_cb(lv_event_t *e) {
  const char *txt = lv_textarea_get_text(s_textarea);
  if (strlen(txt) == 0)
    return;
  chat_send_text(txt);
  lv_textarea_set_text(s_textarea, "");
}

static void page_up_cb(lv_event_t *e) {
  lv_obj_scroll_by(s_chat_viewport, 0, 100, LV_ANIM_ON);
}

static void page_down_cb(lv_event_t *e) {
  lv_obj_scroll_by(s_chat_viewport, 0, -100, LV_ANIM_ON);
}

static void chat_exit_cb(lv_event_t *e) { gs_nav_pop_async(); }

/* 解决问题 2: 手动为图标 Label 指定 LVGL 内置的默认 Symbol 字体 */
static lv_obj_t *create_fab_btn(lv_obj_t *parent, const char *icon,
                                lv_color_t bg_col, lv_color_t text_col) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 38, 38);
  lv_obj_set_style_radius(btn, S_RADIUS_BTN, 0);
  lv_obj_set_style_bg_color(btn, bg_col, 0);
  lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
  lv_obj_set_style_shadow_width(btn, 8, 0);
  lv_obj_set_style_shadow_offset_y(btn, 3, 0);

  lv_obj_t *label = lv_label_create(btn);
  /* 覆盖主题字体，确保使用包含 LV_SYMBOL_* 的默认字体 */
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
  lv_label_set_text(label, icon);
  lv_obj_set_style_text_color(label, text_col, 0);
  lv_obj_center(label);

  return btn;
}

lv_obj_t *chat_comp_create(lv_obj_t *parent) {
  ESP_LOGD(TAG, "Creating chat component with centered FABs...");

  chat_service_init();

  /* 1. 根容器 */
  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(s_root, S_COLOR_BACKGROUND, 0);
  lv_obj_set_style_pad_all(s_root, 0, 0);
  lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

  /* 2. 消息展示区 Viewport */
  s_chat_viewport = lv_obj_create(s_root);
  lv_obj_set_size(s_chat_viewport, 360, 205);
  lv_obj_align(s_chat_viewport, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_flex_flow(s_chat_viewport, LV_FLEX_FLOW_COLUMN);

  /* 左右侧留出足够空间放置居中的 FAB */
  lv_obj_set_style_pad_hor(s_chat_viewport, 52, 0);
  lv_obj_set_style_pad_top(s_chat_viewport, 36, 0);
  lv_obj_set_style_pad_bottom(s_chat_viewport, 10, 0);
  lv_obj_set_style_pad_row(s_chat_viewport, S_GAP, 0);
  lv_obj_set_style_bg_opa(s_chat_viewport, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_chat_viewport, 0, 0);

  for (int i = 0; i < CHAT_WINDOW_SIZE; i++) {
    s_wrappers[i] = lv_obj_create(s_chat_viewport);
    lv_obj_set_size(s_wrappers[i], LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_wrappers[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wrappers[i], 0, 0);
    lv_obj_set_style_pad_all(s_wrappers[i], 0, 0);
    lv_obj_add_flag(s_wrappers[i], LV_OBJ_FLAG_HIDDEN);

    s_bubbles[i] = lv_obj_create(s_wrappers[i]);
    lv_obj_set_width(s_bubbles[i], LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_bubbles[i], LV_PCT(85), 0);
    lv_obj_set_height(s_bubbles[i], LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(s_bubbles[i], S_PAD_V, 0);
    lv_obj_set_style_radius(s_bubbles[i], S_RADIUS_CARD, 0);
    lv_obj_set_style_border_width(s_bubbles[i], 0, 0);

    s_labels[i] = lv_label_create(s_bubbles[i]);
    lv_label_set_long_mode(s_labels[i], LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(s_labels[i], true);
    lv_obj_set_width(s_labels[i], LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_labels[i], &chili_cn, 0);
  }

  /* 解决问题 1.1: 左侧单个 FAB 在左中侧垂直居中 (Y_center = 180) */
  int16_t left_btn_h = 38;
  int16_t left_exit_y = (UI_SCREEN_HEIGHT - left_btn_h) / 2; /* Y = 161 */
  int16_t left_exit_pad =
      ui_circle_get_safe_pad(left_exit_y, left_btn_h) + 6; /* 绝对安全边距 */

  lv_obj_t *exit_btn =
      create_fab_btn(s_root, LV_SYMBOL_LEFT, S_COLOR_SECONDARY_CONTAINER,
                     S_COLOR_ON_SECONDARY_CONTAINER);
  lv_obj_align(exit_btn, LV_ALIGN_TOP_LEFT, left_exit_pad, left_exit_y);
  lv_obj_add_event_cb(exit_btn, chat_exit_cb, LV_EVENT_CLICKED, NULL);

  /* 解决问题 1.2: 右侧 3 个 FAB 在右中侧整体垂直居中 (总高度 = 38*3 + 6*2 =
   * 126px, 起始 Y = 180 - 63 = 117) */
  int16_t gap = 6;
  int16_t fab1_y = (UI_SCREEN_HEIGHT - (38 * 3 + gap * 2)) / 2; /* Y = 117 */
  int16_t fab1_pad = ui_circle_get_safe_pad(fab1_y, 38) + 6;
  lv_obj_t *page_up_btn = create_fab_btn(
      s_root, LV_SYMBOL_UP, S_COLOR_SURFACE_CONTAINER_HIGH, S_COLOR_ON_SURFACE);
  lv_obj_align(page_up_btn, LV_ALIGN_TOP_RIGHT, -fab1_pad, fab1_y);
  lv_obj_add_event_cb(page_up_btn, page_up_cb, LV_EVENT_CLICKED, NULL);

  int16_t fab2_y =
      fab1_y + 38 + gap; /* Y = 161 (最接近圆心 180，算得安全 Pad 最大) */
  int16_t fab2_pad = ui_circle_get_safe_pad(fab2_y, 38) + 6;
  lv_obj_t *page_dn_btn =
      create_fab_btn(s_root, LV_SYMBOL_DOWN, S_COLOR_SURFACE_CONTAINER_HIGH,
                     S_COLOR_ON_SURFACE);
  lv_obj_align(page_dn_btn, LV_ALIGN_TOP_RIGHT, -fab2_pad, fab2_y);
  lv_obj_add_event_cb(page_dn_btn, page_down_cb, LV_EVENT_CLICKED, NULL);

  int16_t fab3_y = fab2_y + 38 + gap; /* Y = 205 */
  int16_t fab3_pad = ui_circle_get_safe_pad(fab3_y, 38) + 6;
  lv_obj_t *send_btn =
      create_fab_btn(s_root, LV_SYMBOL_OK, S_COLOR_PRIMARY, S_COLOR_ON_PRIMARY);
  lv_obj_align(send_btn, LV_ALIGN_TOP_RIGHT, -fab3_pad, fab3_y);
  lv_obj_add_event_cb(send_btn, send_cb, LV_EVENT_CLICKED, NULL);

  /* 5. 底部输入框 */
  int16_t ta_height = 42;
  int16_t ta_bottom_offset = 28;
  int16_t ta_y = UI_SCREEN_HEIGHT - ta_bottom_offset - ta_height; /* Y = 290 */
  int16_t ta_safe_width = ui_circle_get_safe_width(ta_y, ta_height) - 12;

  s_textarea = lv_textarea_create(s_root);
  lv_obj_set_size(s_textarea, ta_safe_width, ta_height);
  lv_obj_align(s_textarea, LV_ALIGN_BOTTOM_MID, 0, -ta_bottom_offset);
  lv_textarea_set_one_line(s_textarea, true);
  lv_textarea_set_placeholder_text(s_textarea, "输入消息...");
  lv_obj_set_style_radius(s_textarea, S_RADIUS_BTN, 0);
  lv_obj_set_style_bg_color(s_textarea, S_COLOR_SURFACE_CONTAINER, 0);
  lv_obj_set_style_border_color(s_textarea, S_COLOR_OUTLINE_VARIANT, 0);
  lv_obj_set_style_border_width(s_textarea, 1, 0);
  lv_obj_set_style_pad_hor(s_textarea, 12, 0);
  lv_obj_set_style_pad_ver(s_textarea, 8, 0);
  lv_obj_set_style_text_font(s_textarea, &chili_cn, 0);

  /* 6. 拼音 IME & 9 键键盘 */
  s_pinyin_ime = lv_ime_pinyin_create(lv_screen_active());
  lv_obj_set_style_text_font(s_pinyin_ime, &chili_cn, 0);

  s_kb = lv_keyboard_create(lv_screen_active());
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_kb, s_textarea);
  lv_ime_pinyin_set_keyboard(s_pinyin_ime, s_kb);

  lv_ime_pinyin_set_mode(s_pinyin_ime, LV_IME_PINYIN_MODE_K9);

  lv_obj_set_size(s_kb, 360, 160);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_pad_hor(s_kb, 28, 0);
  lv_obj_set_style_pad_bottom(s_kb, 10, 0);
  lv_obj_set_style_pad_gap(s_kb, 4, 0);

  lv_obj_t *cand = lv_ime_pinyin_get_cand_panel(s_pinyin_ime);
  if (cand) {
    lv_obj_set_style_text_font(cand, &chili_cn, 0);
    lv_obj_set_size(cand, 260, 32);
    lv_obj_set_style_bg_color(cand, S_COLOR_SURFACE_HIGH, 0);
    lv_obj_set_style_radius(cand, S_RADIUS_SM, 0);
    lv_obj_align_to(cand, s_kb, LV_ALIGN_OUT_TOP_MID, 0, -8);
    lv_obj_add_flag(cand, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_add_event_cb(s_textarea, ta_event_cb, LV_EVENT_ALL, s_kb);

  render_window();

  if (!s_poll_timer) {
    s_poll_timer = lv_timer_create(chat_poll_timer_cb, 100, NULL);
  }

  return s_root;
}

void chat_comp_destroy(void) {
  if (s_poll_timer) {
    lv_timer_del(s_poll_timer);
    s_poll_timer = NULL;
  }
  if (s_pinyin_ime) {
    lv_obj_del(s_pinyin_ime);
    s_pinyin_ime = NULL;
  }
  if (s_kb) {
    lv_obj_del(s_kb);
    s_kb = NULL;
  }
}

void chat_comp_loop(void) { chat_service_loop(); }