#include "chat_comp.h"
#include "chat_fifo.h"
#include "chat_service.h"
#include "gs_portal.h"
#include <src/core/lv_obj_style.h>
#include <src/font/lv_font.h>
#include <src/layouts/flex/lv_flex.h>
#include <src/misc/lv_types.h>
#include <stdio.h>
#include <time.h>

#include "net_adapter.h"

static lv_obj_t *s_bubbles[CHAT_WINDOW_SIZE]; // 气泡容器池
static lv_obj_t *s_labels[CHAT_WINDOW_SIZE];  // 文本标签池
static lv_obj_t *s_textarea;
static lv_obj_t *s_root;
static lv_obj_t *s_chat_viewport;

// 拼音IME回调
static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target_obj(e);
  lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
  if (code == LV_EVENT_FOCUSED) {
    if (lv_indev_get_type(lv_indev_active()) != LV_INDEV_TYPE_KEYPAD) {
      lv_keyboard_set_textarea(kb, ta);
      lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    lv_indev_reset(NULL, ta);
  }
}

static void render_window(void) {
  chat_fifo_t *w = chat_get_window();
  char time_buf[16];

  for (int i = 0; i < CHAT_WINDOW_SIZE; i++) {
    msg_t *m = chat_fifo_get(w, i);

    if (!m) {
      lv_obj_add_flag(s_bubbles[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_remove_flag(s_bubbles[i], LV_OBJ_FLAG_HIDDEN);
    time_t raw_time = (time_t)m->timestamp;
    struct tm *tm_info = localtime(&raw_time);
    if (tm_info) {
      strftime(time_buf, sizeof(time_buf), "%m-%d %H:%M", tm_info);
    } else {
      time_buf[0] = '\0';
    }

    printf("[COMP]:RENDERING MESSAGE sender=%d, id=%ld: %s\r\n", m->sender,
           m->msg_id, m->text);

    lv_label_set_text_fmt(s_labels[i], "[%s]\n%s", time_buf, m->text);

    if (m->sender == 1) { // child
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_hex(0x95EC69), 0);
      lv_obj_set_style_align(s_bubbles[i], LV_ALIGN_RIGHT_MID, 0);
    } else {
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_white(), 0);
      lv_obj_set_style_align(s_bubbles[i], LV_ALIGN_LEFT_MID, 0);
    }
  }
}

static void send_cb(lv_event_t *e) {
  const char *txt = lv_textarea_get_text(s_textarea);
  if (strlen(txt) == 0)
    return;

  printf("[COMP]:SEND CB SNEDED");

  chat_send_text(txt);
  lv_textarea_set_text(s_textarea, "");
}

static void up_btn_cb(lv_event_t *e) {
  chat_fifo_t *w = chat_get_window();
  if (chat_fifo_count(w) > 0) {
    msg_t *oldest = chat_fifo_get(w, 0);
    chat_enter_history(oldest->msg_id, 0);
  }
}

static void down_btn_cb(lv_event_t *e) { chat_enter_live(); }

static lv_obj_t *s_btn_container;

void chat_show_new_msg_toast(void) {
  gs_toast_config_t cfg = {.msg = "new msg",
                           .type = GS_TOAST_INFO,
                           .stay_time = 2000,
                           .click_cb = (void *)chat_enter_live};
  gs_portal_toast_show(cfg);
  printf("[CHAT]:NEW MESSAGE SHOW TAOST\r\n");
}

lv_obj_t *chat_comp_create(lv_obj_t *parent) {
  chat_service_init();
  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));

  s_chat_viewport = lv_obj_create(s_root);
  lv_obj_set_size(s_chat_viewport, LV_PCT(100), LV_PCT(70));

  lv_obj_align(s_chat_viewport, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(s_chat_viewport, LV_FLEX_FLOW_COLUMN);

  for (int i = 0; i < CHAT_WINDOW_SIZE; i++) {
    s_bubbles[i] = lv_obj_create(s_chat_viewport);
    lv_obj_set_size(s_bubbles[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_width(s_bubbles[i], LV_PCT(75)); // 限制气泡最大宽度
    lv_obj_set_style_pad_all(s_bubbles[i], 8, 0);
    lv_obj_set_style_radius(s_bubbles[i], 12, 0);

    s_labels[i] = lv_label_create(s_bubbles[i]);
    lv_obj_set_width(s_labels[i], LV_PCT(100));
    lv_label_set_long_mode(s_labels[i], LV_LABEL_LONG_WRAP);
  }

  s_textarea = lv_textarea_create(s_root);
  lv_obj_set_size(s_textarea, LV_PCT(60), LV_SIZE_CONTENT);
  lv_obj_align(s_textarea, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_textarea_set_one_line(s_textarea, true);

  s_btn_container = lv_obj_create(s_root);
  lv_obj_set_size(s_btn_container, LV_PCT(35), LV_SIZE_CONTENT);
  lv_obj_align(s_btn_container, LV_ALIGN_BOTTOM_RIGHT, -5, -10);
  lv_obj_set_flex_flow(s_btn_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(s_btn_container, 0, 0);

  lv_obj_t *send_btn = lv_btn_create(s_btn_container);
  lv_obj_add_event_cb(send_btn, send_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *send_label = lv_label_create(send_btn);
  lv_label_set_text(send_label, "Send");
  lv_obj_center(send_label);

  lv_obj_t *up_btn = lv_btn_create(s_btn_container);
  lv_obj_add_event_cb(up_btn, up_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *up_label = lv_label_create(up_btn);
  lv_label_set_text(up_label, "Up");
  lv_obj_center(up_label);

  lv_obj_t *down_btn = lv_btn_create(s_btn_container);
  lv_obj_add_event_cb(down_btn, down_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *down_label = lv_label_create(down_btn);
  lv_label_set_text(down_label, "Down");
  lv_obj_center(down_label);

  // 将拼音IME系统挂载在全局屏幕而非局部视口，防止弹窗被截断
  lv_obj_t *pinyin_ime = lv_ime_pinyin_create(lv_screen_active());

  lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
  lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
  lv_keyboard_set_textarea(kb, s_textarea);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(s_textarea, ta_event_cb, LV_EVENT_ALL, kb);

  lv_obj_t *cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
  lv_obj_set_size(cand_panel, LV_PCT(100), LV_PCT(10));
  lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);

  chat_enter_live();

  return s_root;
}

// void chat_comp_loop(void) {
//   chat_service_loop();
//   if (chat_window_is_dirty()) {
//     render_window();
//     chat_window_clear_dirty();
//   }
// }

void chat_comp_loop() {
  chat_service_loop();
  if (chat_window_is_dirty()) {
    render_window();
    chat_window_clear_dirty();

    lv_obj_scroll_to_view(s_bubbles[CHAT_WINDOW_SIZE - 1], LV_ANIM_ON);
  }
}