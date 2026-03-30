/* menu_page.c */
#include "gs_nav.h"
#include "string.h"
// 菜单点击回调
static void menu_item_click_cb(lv_event_t *e) {
  const char *txt = (const char *)lv_event_get_user_data(e);
  if (strcmp(txt, "BACK") == 0) {
    gs_nav_pop();
  }
}

static lv_obj_t *menu_page_render(lv_obj_t *parent, void *ctx) {
  lv_obj_t *list = lv_list_create(parent);
  lv_obj_set_size(list, 320, 240);

  // 添加列表项
  lv_obj_t *btn;

  btn = lv_list_add_btn(list, LV_SYMBOL_LEFT, "Back to Lock");
  lv_obj_add_event_cb(btn, menu_item_click_cb, LV_EVENT_CLICKED,
                      (void *)"BACK");

  lv_list_add_btn(list, LV_SYMBOL_WIFI, "Family Chat");
  lv_list_add_btn(list, LV_SYMBOL_CALL, "AI Voice");
  lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "Settings");

  return list;
}

const gs_page_desc_t page_menu = {
    .init_cb = NULL, .render_cb = menu_page_render, .deinit_cb = NULL};