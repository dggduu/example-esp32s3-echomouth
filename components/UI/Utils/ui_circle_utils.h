#ifndef UI_CIRCLE_UTILS_H
#define UI_CIRCLE_UTILS_H

#include "StyleSheet.h"
#include "lvgl.h"
#include "ui_circle_toolkit.h"

/**
 * @brief 创建统一 UI 风格的 FAB / 圆形按键
 */
static inline lv_obj_t *ui_circle_create_fab(lv_obj_t *parent, const char *icon,
                                             lv_color_t bg_col,
                                             lv_color_t text_col) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 36, 36);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, bg_col, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, icon);
  lv_obj_center(label);
  lv_obj_set_style_text_color(label, text_col, 0);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);

  return btn;
}

/**
 * @brief 在容器左侧安全区添加统一的“返回/退出”按钮
 */
/**
 * @brief 在父对象左侧中部添加悬浮返回按钮（不参与 Flex/Grid 布局）
 */
static inline lv_obj_t *ui_circle_add_exit_btn(lv_obj_t *parent,
                                               lv_event_cb_t exit_cb) {
  lv_obj_t *exit_btn =
      ui_circle_create_fab(parent, LV_SYMBOL_LEFT,
                           S_COLOR_SURFACE_CONTAINER_HIGH, S_COLOR_ON_SURFACE);

  /* 关键：脱离 Flex/Grid 布局 */
  lv_obj_add_flag(exit_btn, LV_OBJ_FLAG_FLOATING);

  /* 保证显示在最上层 */
  lv_obj_move_foreground(exit_btn);

  /* 左侧中间 */
  lv_obj_align(exit_btn, LV_ALIGN_LEFT_MID, 20, 0);

  if (exit_cb) {
    lv_obj_add_event_cb(exit_btn, exit_cb, LV_EVENT_CLICKED, NULL);
  }

  return exit_btn;
}

/**
 * @brief 创建统一风格的 Spinner 加载指示器
 */
/**
 * @brief 创建统一风格的 Spinner 加载指示器 (LVGL v9)
 */
static inline lv_obj_t *ui_circle_create_spinner(lv_obj_t *parent) {
  // LVGL v9 仅接收 parent 1 个参数
  lv_obj_t *spinner = lv_spinner_create(parent);
  lv_obj_set_size(spinner, 36, 36);

  // 如需在 LVGL v9 中设置旋转时间(ms)和弧长(角度deg)，可使用：
  // lv_spinner_set_anim_params(spinner, 1000, 60);

  // 底圈样式 (PART_MAIN)
  lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner, S_COLOR_SURFACE_CONTAINER_HIGH,
                             LV_PART_MAIN);
  lv_obj_set_style_arc_opa(spinner, LV_OPA_COVER, LV_PART_MAIN);

  // 旋转弧线样式 (PART_INDICATOR)
  lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, S_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(spinner, true, LV_PART_INDICATOR);

  lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(spinner, 0, 0);
  lv_obj_center(spinner);

  return spinner;
}

#endif /* UI_CIRCLE_UTILS_H */