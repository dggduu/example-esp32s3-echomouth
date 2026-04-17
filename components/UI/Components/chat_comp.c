#include "chat_comp.h"
#include "chat_service.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "CHAT_COMP";

static lv_obj_t *s_bubbles[CHAT_WINDOW_SIZE];
static lv_obj_t *s_labels[CHAT_WINDOW_SIZE];
static lv_obj_t *s_wrappers[CHAT_WINDOW_SIZE];
static lv_obj_t *s_textarea;
static lv_obj_t *s_root;
static lv_obj_t *s_chat_viewport;

LV_FONT_DECLARE(chinese_font_14px);

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

    lv_label_set_text_fmt(s_labels[i], "#888888 %s#\n%s", time_buf, m->text);

    if (m->sender == 1) {
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_hex(0x95EC69), 0);
      lv_obj_set_flex_align(s_wrappers[i], LV_FLEX_ALIGN_END,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_white(), 0);
      lv_obj_set_flex_align(s_wrappers[i], LV_FLEX_ALIGN_START,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
  }
  lv_obj_update_layout(s_chat_viewport);
  lv_obj_scroll_to_view(s_wrappers[CHAT_WINDOW_SIZE - 1], LV_ANIM_OFF);
}

static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target_obj(e);
  lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_chat_viewport, LV_PCT(40));
  } else if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_chat_viewport, LV_PCT(74));
  }
}

static void send_cb(lv_event_t *e) {
  const char *txt = lv_textarea_get_text(s_textarea);
  if (strlen(txt) == 0)
    return;
  chat_send_text(txt);
  lv_textarea_set_text(s_textarea, "");
}

static void chat_exit_cb(lv_event_t *e) { gs_nav_pop_async(); }

static void up_btn_cb(lv_event_t *e) {
  int count = chat_fifo_count();
  msg_t *oldest = (count > 0) ? chat_fifo_get(0) : NULL;
  chat_enter_history(oldest ? oldest->msg_id : 0xFFFFFFFF, HISTORY_DIR_OLDER);
}

static void down_btn_cb(lv_event_t *e) { chat_enter_live(); }

static void async_render_window(void *dummy) { render_window(); }
static void on_chat_dirty() { lv_async_call(async_render_window, NULL); }

lv_obj_t *chat_comp_create(lv_obj_t *parent) {
  chat_service_init();

  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_root, 0, 0);
  lv_obj_set_style_pad_row(s_root, 0, 0);

  /* Header */
  lv_obj_t *header = lv_obj_create(s_root);
  lv_obj_set_size(header, LV_PCT(100), LV_PCT(10));
  lv_obj_set_style_pad_ver(header, 0, 0);

  lv_obj_t *exit_btn = lv_btn_create(header);
  lv_obj_set_size(exit_btn, 40, 30);
  lv_obj_align(exit_btn, LV_ALIGN_LEFT_MID, 5, 0);
  lv_obj_t *exit_icon = lv_label_create(exit_btn);
  lv_label_set_text(exit_icon, LV_SYMBOL_LEFT);
  lv_obj_center(exit_icon);
  lv_obj_add_event_cb(exit_btn, chat_exit_cb, LV_EVENT_CLICKED, NULL);

  /* Chat */
  s_chat_viewport = lv_obj_create(s_root);
  lv_obj_set_size(s_chat_viewport, LV_PCT(100), LV_PCT(74));
  lv_obj_set_flex_flow(s_chat_viewport, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_chat_viewport, 8, 0);
  lv_obj_set_style_pad_row(s_chat_viewport, 8, 0);
  lv_obj_set_style_bg_color(s_chat_viewport, lv_color_hex(0xF0F0F0), 0);

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
    lv_obj_set_style_pad_all(s_bubbles[i], 8, 0);
    lv_obj_set_style_radius(s_bubbles[i], 8, 0);

    s_labels[i] = lv_label_create(s_bubbles[i]);
    lv_label_set_long_mode(s_labels[i], LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(s_labels[i], true);
    lv_obj_set_width(s_labels[i], LV_SIZE_CONTENT);
  }

  /* Footer */
  lv_obj_t *footer = lv_obj_create(s_root);
  lv_obj_set_size(footer, LV_PCT(100), LV_PCT(16));
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(footer, 4, 0);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(footer, 5, 0);

  s_textarea = lv_textarea_create(footer);
  lv_obj_set_flex_grow(s_textarea, 1);
  lv_textarea_set_one_line(s_textarea, true);

  const struct {
    const char *sym;
    lv_event_cb_t cb;
  } btns[] = {{LV_SYMBOL_UP, up_btn_cb},
              {LV_SYMBOL_DOWN, down_btn_cb},
              {LV_SYMBOL_OK, send_cb}};

  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = lv_btn_create(footer);
    lv_obj_set_size(b, 35, 35);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, btns[i].sym);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, btns[i].cb, LV_EVENT_CLICKED, NULL);
  }

  /* Keyboard */
  lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *pinyin_ime = lv_ime_pinyin_create(lv_screen_active());
  lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
  lv_obj_add_event_cb(s_textarea, ta_event_cb, LV_EVENT_ALL, kb);

  chat_service_register_render_cb(on_chat_dirty);
  return s_root;
}

void chat_comp_loop(void) { chat_service_loop(); }