#include "chat_comp.h"
#include "chat_fifo.h"
#include "chat_service.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include <time.h>


static const char *TAG = "CHAT_COMP";

static lv_obj_t *s_bubbles[CHAT_WINDOW_SIZE];
static lv_obj_t *s_labels[CHAT_WINDOW_SIZE];
static lv_obj_t *s_textarea;
static lv_obj_t *s_root;
static lv_obj_t *s_chat_viewport;

LV_FONT_DECLARE(chinese_font_14px);

/* ---------- 拼音 IME 回调 ---------- */
static void ta_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target_obj(e);
  lv_obj_t *kb = lv_event_get_user_data(e);
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

/* ---------- 渲染聊天窗口 ---------- */
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

    lv_label_set_text_fmt(s_labels[i], "[%s]\n%s", time_buf, m->text);
    lv_obj_set_style_text_font(s_bubbles[i], &chinese_font_14px, 0);

    if (m->sender == 1) { // 孩子消息（右对齐，绿色气泡）
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_hex(0x95EC69), 0);
      lv_obj_set_style_align(s_bubbles[i], LV_ALIGN_RIGHT_MID, 0);
    } else { // 家长消息（左对齐，白色气泡）
      lv_obj_set_style_bg_color(s_bubbles[i], lv_color_white(), 0);
      lv_obj_set_style_align(s_bubbles[i], LV_ALIGN_LEFT_MID, 0);
    }
  }
}

/* ---------- 发送按钮回调 ---------- */
static void send_cb(lv_event_t *e) {
  const char *txt = lv_textarea_get_text(s_textarea);
  if (strlen(txt) == 0)
    return;
  chat_send_text(txt);
  lv_textarea_set_text(s_textarea, "");
}

/* ---------- 退出按钮回调 ---------- */
static void chat_exit_cb(lv_event_t *e) { gs_nav_pop_async(); }

/* ---------- 向上翻历史 ---------- */
static void up_btn_cb(lv_event_t *e) {
  chat_fifo_t *w = chat_get_window();
  if (chat_fifo_count(w) > 0) {
    msg_t *oldest = chat_fifo_get(w, 0);
    chat_enter_history(oldest->msg_id, 0);
  }
}

/* ---------- 向下回到实时 ---------- */
static void down_btn_cb(lv_event_t *e) { chat_enter_live(); }

/* ---------- 新消息 Toast 回调 ---------- */
void chat_show_new_msg_toast(void) {
  gs_toast_config_t cfg = {.msg = "新消息",
                           .type = GS_TOAST_INFO,
                           .stay_time = 2000,
                           .click_cb = (void *)chat_enter_live};
  gs_portal_toast_show(cfg);
}

/* ---------- 创建图标按钮辅助函数 ---------- */
static lv_obj_t *create_icon_btn(lv_obj_t *parent, const char *icon_text,
                                 lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 44, 44);
  lv_obj_set_style_radius(btn, 22, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xEEEEEE), 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, icon_text);
  lv_obj_center(label);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24,
                             0); // 使用较大字体显示图标
  return btn;
}

/* ---------- 主创建函数 ---------- */
lv_obj_t *chat_comp_create(lv_obj_t *parent) {
  chat_service_init();

  /* 根容器 */
  s_root = lv_obj_create(parent);
  lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_root, 8, 0);
  lv_obj_set_style_bg_color(s_root, lv_color_hex(0xF5F5F5), 0);

  /* ---------- 顶部栏 ---------- */
  lv_obj_t *header = lv_obj_create(s_root);
  lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(header, 8, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(header, 0, 0);

  // 标题
  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "聊天");
  lv_obj_set_style_text_font(title, &chinese_font_14px, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);

  // 占位弹簧
  lv_obj_t *spacer = lv_obj_create(header);
  lv_obj_set_flex_grow(spacer, 1);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(spacer, 0, 0);

  // 退出按钮（使用叉号图标）
  create_icon_btn(header, LV_SYMBOL_CLOSE, chat_exit_cb);

  /* ---------- 聊天消息区域 ---------- */
  s_chat_viewport = lv_obj_create(s_root);
  lv_obj_set_flex_grow(s_chat_viewport, 1);
  lv_obj_set_flex_flow(s_chat_viewport, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_chat_viewport, LV_DIR_VER);
  lv_obj_set_style_pad_all(s_chat_viewport, 8, 0);
  lv_obj_set_style_bg_color(s_chat_viewport, lv_color_hex(0xF5F5F5), 0);
  lv_obj_set_style_border_width(s_chat_viewport, 0, 0);

  for (int i = 0; i < CHAT_WINDOW_SIZE; i++) {
    s_bubbles[i] = lv_obj_create(s_chat_viewport);
    lv_obj_set_width(s_bubbles[i], LV_PCT(80));
    lv_obj_set_height(s_bubbles[i], LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(s_bubbles[i], 10, 0);
    lv_obj_set_style_radius(s_bubbles[i], 16, 0);
    lv_obj_set_style_shadow_width(s_bubbles[i], 2, 0);
    lv_obj_set_style_shadow_color(s_bubbles[i], lv_color_hex(0xCCCCCC), 0);

    s_labels[i] = lv_label_create(s_bubbles[i]);
    lv_obj_set_width(s_labels[i], LV_PCT(100));
    lv_label_set_long_mode(s_labels[i], LV_LABEL_LONG_WRAP);
  }

  /* ---------- 底部输入栏 ---------- */
  lv_obj_t *footer = lv_obj_create(s_root);
  lv_obj_set_size(footer, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(footer, 8, 0);
  lv_obj_set_style_bg_color(footer, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(footer, 0, 0);

  // 向上翻页按钮
  create_icon_btn(footer, LV_SYMBOL_UP, up_btn_cb);
  // 向下翻页按钮
  create_icon_btn(footer, LV_SYMBOL_DOWN, down_btn_cb);

  // 文本输入框
  s_textarea = lv_textarea_create(footer);
  lv_obj_set_height(s_textarea, 40);
  lv_obj_set_flex_grow(s_textarea, 1);
  lv_textarea_set_one_line(s_textarea, true);
  lv_obj_set_style_bg_color(s_textarea, lv_color_hex(0xF0F0F0), 0);
  lv_obj_set_style_border_width(s_textarea, 0, 0);
  lv_obj_set_style_radius(s_textarea, 20, 0);
  lv_obj_set_style_pad_left(s_textarea, 12, 0);

  // 发送按钮
  create_icon_btn(footer, LV_SYMBOL_OK, send_cb);

  /* ---------- 拼音输入法 ---------- */
  lv_obj_t *pinyin_ime = lv_ime_pinyin_create(lv_screen_active());
  lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
  lv_obj_set_style_text_font(kb, &chinese_font_14px, 0);

  lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
  lv_keyboard_set_textarea(kb, s_textarea);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_textarea, ta_event_cb, LV_EVENT_ALL, kb);

  lv_obj_t *cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
  lv_obj_set_size(cand_panel, LV_PCT(100), LV_PCT(12));
  lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);

  chat_enter_live();
  return s_root;
}

/* ---------- UI 轮询更新 ---------- */
void chat_comp_loop(void) {
  if (chat_window_is_dirty()) {
    render_window();
    chat_window_clear_dirty();

    // 自动滚动到最新消息
    lv_obj_scroll_to_view(s_bubbles[CHAT_WINDOW_SIZE - 1], LV_ANIM_ON);
  }
}