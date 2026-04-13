#include "my_theme.h"
#include "StyleSheet.h"
#include "lvgl.h"

LV_FONT_DECLARE(chinese_font_14px);

static void my_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  lv_font_t *font = &chinese_font_14px;
  // 逻辑：将中文字体作为默认回退，确保即使逻辑漏掉类名也能显示
  font->fallback = &lv_font_montserrat_14;

  // 针对所有对象的基础文字样式（解决键盘/候选栏方框的核心）
  lv_obj_set_style_text_font(obj, font, 0);

  if (lv_obj_check_type(obj, &lv_button_class)) {
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 4, 0); // 使用具体数值或宏
    lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_BLUE), 0);
  }

  // // 显式处理按钮矩阵（键盘的核心类）
  // if (lv_obj_check_type(obj, &lv_btnma)) {
  //   lv_obj_set_style_text_font(obj, font, 0);
  // }
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

  // 关键：设置全局默认字体，防止未识别组件漏刷
  lv_obj_set_style_text_font(lv_screen_active(), &chinese_font_14px, 0);
}