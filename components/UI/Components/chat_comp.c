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
static lv_obj_t *s_wrappers[CHAT_WINDOW_SIZE]; // 行容器
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
    lv_obj_clear_flag(s_wrappers[i], LV_OBJ_FLAG_HIDDEN);

    time_t raw = (time_t)m->timestamp;
    struct tm *tm_info = localtime(&raw);
    if (tm_info && m->timestamp != 0) {
      strftime(time_buf, sizeof(time_buf), "%m-%d %H:%M", tm_info);
    } else {
      strcpy(time_buf, "Now");
    }

    lv_label_set_text_fmt(s_labels[i], "%s\n%s", time_buf, m->text);
    lv_obj_set_style_text_font(s_bubbles[i], &chinese_font_14px, 0);

    if (m->sender == 1) {
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_hex(0x95EC69), 0);
      lv_obj_set_flex_align(s_wrappers[i], LV_FLEX_ALIGN_END,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_white(), 0);
      lv_obj_set_flex_align(s_wrappers[i], LV_FLEX_ALIGN_START,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    // ESP_LOGI(TAG, "msg: id=%lu, timestamp=%lu, sender=%d, text=%s",
    // m->msg_id,
    //          m->timestamp, m->sender, m->text);
  }
  // 滚动到底部
  lv_obj_update_layout(s_chat_viewport);
  lv_obj_scroll_to_view(s_wrappers[CHAT_WINDOW_SIZE - 1], LV_ANIM_ON);
}

/* 拼音IME事件 */
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

static void send_cb(lv_event_t *e) {
  const char *txt = lv_textarea_get_text(s_textarea);
  if (strlen(txt) == 0)
    return;
  chat_send_text(txt);
  lv_textarea_set_text(s_textarea, "");
}

static void chat_exit_cb(lv_event_t *e) {
  // chat_exit_chat();
  gs_nav_pop_async();
}

static void up_btn_cb(lv_event_t *e) {
  int count = chat_fifo_count();
  if (count > 0) {
    msg_t *oldest = chat_fifo_get(0); // 索引0是最早的消息
    if (oldest) {
      chat_enter_history(oldest->msg_id, HISTORY_DIR_OLDER);
    }
  } else {
    // 没有消息时请求最新的一批（lastMsgId = 0xFFFFFFFF）
    chat_enter_history(0xFFFFFFFF, HISTORY_DIR_OLDER);
  }
}

static void async_render_window(void *dummy) { render_window(); }
static void on_chat_dirty() { lv_async_call(async_render_window, NULL); }

static void down_btn_cb(lv_event_t *e) { chat_enter_live(); }

static void on_notify_received(uint32_t msg_id, uint8_t sender,
                               const char *preview) {
  // 构造提示文本
  char toast_msg[128];
  const char *sender_name = (sender == 1) ? "Child" : "Parent";
  snprintf(toast_msg, sizeof(toast_msg), "New message from %s: %s", sender_name,
           preview);

  gs_toast_config_t cfg = {
      .msg = toast_msg,
      .type = GS_TOAST_INFO,
      .stay_time = 3000,
      .click_cb = NULL // 可选点击后进入实时模式
  };
  gs_portal_toast_show(cfg);
}

lv_obj_t *chat_comp_create(lv_obj_t *parent) {
  chat_service_init();

  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_root, 0, 0);

  /* Header */
  lv_obj_t *header = lv_obj_create(s_root);
  lv_obj_set_size(header, LV_PCT(100), LV_PCT(18));
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(header, 5, 0);

  lv_obj_t *exit_btn = lv_btn_create(header);
  lv_obj_set_size(exit_btn, 70, 35);
  lv_obj_t *exit_label = lv_label_create(exit_btn);
  lv_label_set_text(exit_label, "Exit");
  lv_obj_center(exit_label);
  lv_obj_add_event_cb(exit_btn, chat_exit_cb, LV_EVENT_CLICKED, NULL);

  /* Chat viewport */
  s_chat_viewport = lv_obj_create(s_root);
  lv_obj_set_size(s_chat_viewport, LV_PCT(100), LV_PCT(60));
  lv_obj_set_flex_flow(s_chat_viewport, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_chat_viewport, LV_DIR_VER);

  for (int i = 0; i < CHAT_WINDOW_SIZE; i++) {
    // 行容器（Flex 行，用于控制左右对齐）
    s_wrappers[i] = lv_obj_create(s_chat_viewport);
    lv_obj_set_width(s_wrappers[i], LV_PCT(100));
    lv_obj_set_height(s_wrappers[i], LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_wrappers[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wrappers[i], 0, 0);
    lv_obj_set_style_pad_all(s_wrappers[i], 0, 0);
    lv_obj_set_flex_flow(s_wrappers[i], LV_FLEX_FLOW_ROW);
    lv_obj_add_flag(s_wrappers[i], LV_OBJ_FLAG_HIDDEN);

    s_bubbles[i] = lv_obj_create(s_wrappers[i]);
    lv_obj_set_width(s_bubbles[i], LV_PCT(75));
    lv_obj_set_height(s_bubbles[i], LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(s_bubbles[i], 8, 0);
    lv_obj_set_style_radius(s_bubbles[i], 12, 0);

    s_labels[i] = lv_label_create(s_bubbles[i]);
    lv_obj_set_width(s_labels[i], LV_PCT(100));
    lv_label_set_long_mode(s_labels[i], LV_LABEL_LONG_WRAP);
  }

  /* Footer */
  lv_obj_t *footer = lv_obj_create(s_root);
  lv_obj_set_size(footer, LV_PCT(100), LV_PCT(18));
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  s_textarea = lv_textarea_create(footer);
  lv_obj_set_width(s_textarea, LV_PCT(50));
  lv_textarea_set_one_line(s_textarea, true);

  lv_obj_t *send_btn = lv_btn_create(footer);
  lv_obj_set_size(send_btn, 60, 40);
  lv_obj_add_event_cb(send_btn, send_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *send_label = lv_label_create(send_btn);
  lv_label_set_text(send_label, "Send");
  lv_obj_center(send_label);

  lv_obj_t *up_btn = lv_btn_create(footer);
  lv_obj_set_size(up_btn, 50, 40);
  lv_obj_add_event_cb(up_btn, up_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *up_label = lv_label_create(up_btn);
  lv_label_set_text(up_label, "U");
  lv_obj_center(up_label);

  lv_obj_t *down_btn = lv_btn_create(footer);
  lv_obj_set_size(down_btn, 60, 40);
  lv_obj_add_event_cb(down_btn, down_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *down_label = lv_label_create(down_btn);
  lv_label_set_text(down_label, "D");
  lv_obj_center(down_label);

  /* IME */
  lv_obj_t *pinyin_ime = lv_ime_pinyin_create(lv_screen_active());
  lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
  // lv_obj_set_style_text_font(kb, &chinese_font_14px, 0);
  lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
  lv_keyboard_set_textarea(kb, s_textarea);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_textarea, ta_event_cb, LV_EVENT_ALL, kb);

  lv_obj_t *cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
  lv_obj_set_size(cand_panel, LV_PCT(100), LV_PCT(10));
  lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);

  chat_service_register_render_cb(on_chat_dirty);
  chat_service_register_notify_cb(on_notify_received);
  // chat_enter_live();
  return s_root;
}

void chat_comp_loop(void) {
  chat_service_loop();
  // if (chat_window_is_dirty()) {
  //   ESP_LOGI("CHAT_COMP", "Window dirty, rendering...");
  //   render_window();

  //   chat_window_clear_dirty();
  // }
}