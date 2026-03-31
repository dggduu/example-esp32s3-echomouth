#include "my_theme.h"
#include "StyleSheet.h"
#include "lvgl.h"

// 声明外部字体
LV_FONT_DECLARE(chinese_font_14px);

static void my_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  // 默认应用：如果对象支持文本，设置其字体
  // 注意：在 apply_cb 中通常通过 th 参数获取字体，但由于封装限制，
  // 我们直接使用全局声明的字体变量是最安全且最高效的。

  // btn 逻辑
  if (lv_obj_check_type(obj, &lv_button_class)) {
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, EL_RADIUS_BORDER_BASE, 0);
    lv_obj_set_style_bg_color(obj, EL_COLOR_PRIMARY, 0);

    // 修正：直接使用字体指针
    lv_obj_set_style_text_font(obj, &chinese_font_14px, 0);
  }

  // 如果是标签，也默认应用字体
  if (lv_obj_check_type(obj, &lv_label_class)) {
    lv_obj_set_style_text_font(obj, &chinese_font_14px, 0);
  }
}

void my_ui_theme_init(void) {
  lv_display_t *disp = lv_display_get_default();
  if (!disp)
    return;

  // 创建自定义主题
  lv_theme_t *my_theme = lv_theme_create();

  // 关键修正：
  // LVGL 不允许直接给 theme->font_normal 赋值。
  // 正确做法是在 apply_cb 中根据逻辑分配字体，
  // 或者使用内置的默认主题初始化函数。

  lv_theme_set_apply_cb(my_theme, my_theme_apply_cb);

  // 设置父级主题以继承未定义的样式
  lv_theme_t *th_act = lv_display_get_theme(disp);
  lv_theme_set_parent(my_theme, th_act);

  // 将自定义主题设置为当前显示的活动主题
  lv_display_set_theme(disp, my_theme);
}