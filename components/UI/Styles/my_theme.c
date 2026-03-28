#include "my_theme.h"
#include "StyleSheet.h"
#include "lvgl.h"
#include <src/font/lv_font.h>

static void my_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  // btn
  if (lv_obj_check_type(obj, &lv_button_class)) {
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, EL_RADIUS_BORDER_BASE, 0);
    lv_obj_set_style_bg_color(obj, EL_COLOR_PRIMARY, 0);
  }
}

void my_ui_theme_init(void) {
  lv_display_t *disp = lv_display_get_default();
  if (!disp)
    return;
  lv_theme_t *my_theme = lv_theme_create();
  lv_theme_set_apply_cb(my_theme, my_theme_apply_cb);
  lv_theme_t *th_act = lv_display_get_theme(disp);
  lv_theme_set_parent(my_theme, th_act);
  lv_display_set_theme(disp, my_theme);
}