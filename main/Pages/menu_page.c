#include "StyleSheet.h"
#include "font/lv_symbol_def.h"
#include "gs_nav.h"
#include <string.h>

#include "nvs_helper.h"

extern const gs_page_desc_t page_chat;
extern const gs_page_desc_t page_cam;
extern const gs_page_desc_t page_todo;
extern const gs_page_desc_t page_ota;
extern const gs_page_desc_t page_debug;
extern const gs_page_desc_t page_cam_test;

static void menu_item_click_cb(lv_event_t *e) {
  const char *txt = (const char *)lv_event_get_user_data(e);
  if (!txt)
    return;
  if (strcmp(txt, "HOME") == 0) {
    gs_nav_pop();
  } else if (strcmp(txt, "CHAT") == 0) {
    gs_nav_push(&page_chat, NULL);
  } else if (strcmp(txt, "DEBUG") == 0) {
    gs_nav_push(&page_debug, NULL);
  } else if (strcmp(txt, "TODO") == 0) {
    int did = nvs_helper_get_did();
    if (did == -1)
      return;
    // device_id 必须用堆分配，因为 gs_nav_push_async 是延迟回调
    int32_t *p_did = malloc(sizeof(int32_t));
    if (!p_did)
      return;
    *p_did = did;
    gs_nav_push_async(&page_todo, p_did);
  }
}

static lv_obj_t *menu_page_render(lv_obj_t *parent, void *ctx) {

  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  /* 清除主容器的上下边距 */
  lv_obj_set_style_pad_top(cont, 20, 0);
  lv_obj_set_style_pad_bottom(cont, 0, 0);
  lv_obj_set_style_pad_hor(cont, 28, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  /* title - 取消底部 margin */
  lv_obj_t *title = lv_label_create(cont);
  lv_label_set_text(title, "MENU");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, S_TEXT_PRIMARY, 0);
  lv_obj_set_style_margin_bottom(title, 8, 0);

  /* menu list - 移除底部间距 */
  lv_obj_t *list = lv_list_create(cont);
  lv_obj_set_size(list, LV_PCT(85), LV_PCT(80));
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_row(list, S_GAP, 0);
  lv_obj_set_style_pad_bottom(list, 0, 0); // 取消列表容器底部的 padding

  struct {
    const char *icon;
    const char *name;
    const char *data;
  } items[] = {
      {LV_SYMBOL_HOME, "主页", "HOME"},
      {LV_SYMBOL_CALL, "聊天", "CHAT"},
      {LV_SYMBOL_LIST, "待办", "TODO"},
      {LV_SYMBOL_SETTINGS, "调试", "DEBUG"},
  };

  for (int i = 0; i < 4; i++) {
    lv_obj_t *btn = lv_list_add_btn(list, items[i].icon, items[i].name);

    lv_obj_set_style_bg_color(btn, S_BG_CARD, 0);
    lv_obj_set_style_radius(btn, S_RADIUS_CARD, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_ver(btn, 14, 0);
    lv_obj_set_style_pad_hor(btn, S_PAD_H, 0);

    lv_obj_set_style_text_color(btn, S_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);

    /* pressed state: primary container */
    lv_obj_set_style_bg_color(btn, S_COLOR_PRIMARY_CONTAINER, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, S_COLOR_ON_PRIMARY_CONTAINER,
                                LV_STATE_PRESSED);

    if (items[i].data) {
      lv_obj_add_event_cb(btn, menu_item_click_cb, LV_EVENT_CLICKED,
                          (void *)items[i].data);
    }
  }

  return cont;
}

const gs_page_desc_t page_menu = {
    .init_cb = NULL, .render_cb = menu_page_render, .deinit_cb = NULL};