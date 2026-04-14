/* menu_page.c */
#include "gs_nav.h"
#include <string.h>

#include "time_test_helper.h"

#define MENU_PRIMARY_COLOR lv_color_hex(0x409eff)
#define MENU_BG_COLOR lv_color_hex(0xf0f0f0)
#define MENU_TEXT_COLOR lv_color_hex(0x333333)

extern const gs_page_desc_t page_chat;
extern const gs_page_desc_t page_cam;
extern const gs_page_desc_t page_todo;
extern const gs_page_desc_t page_ota;

static void menu_item_click_cb(lv_event_t *e) {
  const char *txt = (const char *)lv_event_get_user_data(e);
  TEST_MEM_INFO("MENU");
  if (!txt)
    return;
  if (strcmp(txt, "HOME") == 0) {
    gs_nav_pop();
  } else if (strcmp(txt, "CHAT") == 0) {
    gs_nav_push(&page_chat, NULL);
  } else if (strcmp(txt, "TODO") == 0) {
    int device_id = 1;
    gs_nav_push_async(&page_todo, &device_id);
  }
  //  else if (strcmp(txt, "CAM") == 0) {
  //   int params = 4;
  //   gs_nav_push(&page_cam, &params);
  // }
}

static lv_obj_t *menu_page_render(lv_obj_t *parent, void *ctx) {
  // 创建背景容器，设置背景色
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(cont, MENU_BG_COLOR, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 15, 0);
  lv_obj_set_style_pad_top(cont, 20, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  // 标题栏
  lv_obj_t *title = lv_label_create(cont);
  lv_label_set_text(title, "Menu");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_margin_bottom(title, 10, 0);

  // 创建列表
  lv_obj_t *list = lv_list_create(cont);
  lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(list, 0, 0);       // 列表背景透明
  lv_obj_set_style_border_width(list, 0, 0); // 去掉外边框
  lv_obj_set_style_pad_row(list, 8, 0);

  struct {
    const char *icon;
    const char *name;
    const char *data;
  } items[] =
      {
          {LV_SYMBOL_HOME, "主页", "HOME"},
          {LV_SYMBOL_CALL, "聊天", "CHAT"},
          {LV_SYMBOL_LIST, "ToDoList", "TODO"},
          // {LV_SYMBOL_PLAY, "相机", "CAM"},
      }

  ;

  for (int i = 0; i < 3; i++) {
    lv_obj_t *btn = lv_list_add_btn(list, items[i].icon, items[i].name);

    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_ver(btn, 12, 0);
    // 文字颜色和字体
    lv_obj_set_style_text_color(btn, MENU_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);

    // 按压
    lv_obj_set_style_bg_color(btn, MENU_PRIMARY_COLOR, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_PRESSED);

    if (items[i].data) {
      lv_obj_add_event_cb(btn, menu_item_click_cb, LV_EVENT_CLICKED,
                          (void *)items[i].data);
    }
  }

  return cont;
}

const gs_page_desc_t page_menu = {
    .init_cb = NULL, .render_cb = menu_page_render, .deinit_cb = NULL};