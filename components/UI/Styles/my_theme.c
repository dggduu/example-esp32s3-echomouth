#include "my_theme.h"
#include "StyleSheet.h"
#include "lvgl.h"

LV_FONT_DECLARE(chili_cn);

static lv_style_t style_screen;
static lv_style_t style_btn;
static lv_style_t style_btn_pressed;
static lv_style_t style_card;
static lv_style_t style_label;

static void my_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  const lv_font_t *font = &chili_cn;

  /* default text color for all objects */
  lv_obj_set_style_text_color(obj, S_TEXT_PRIMARY, LV_PART_MAIN);

  /* Screen */
  if (obj == lv_screen_active()) {
    lv_obj_add_style(obj, &style_screen, LV_PART_MAIN);
    return;
  }

  /* Button */
  if (lv_obj_check_type(obj, &lv_button_class)) {
    lv_obj_add_style(obj, &style_btn, LV_PART_MAIN);
    lv_obj_add_style(obj, &style_btn_pressed, LV_STATE_PRESSED);
  }

  /* Label — use Chinese font, icons keep default */
  if (lv_obj_check_type(obj, &lv_label_class)) {
    lv_obj_add_style(obj, &style_label, LV_PART_MAIN);
    /* only apply Chinese font to non-icon labels */
    const char *txt = lv_label_get_text(obj);
    if (txt && txt[0] != '\xEF') { /* LV_SYMBOL_* starts with 0xEF */
      lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    }
  }

  /*
   * Card 样式不自动应用。
   * 页面如需卡片容器，请手动调用:
   *   lv_obj_add_style(container, &style_card, 0);
   * 或使用 StyleSheet 中的 S_RADIUS_CARD / S_BG_CARD 等宏。
   */
}

void my_ui_theme_init(void) {

  /*
   * Screen
   */

  lv_style_init(&style_screen);

  lv_style_set_bg_color(&style_screen, S_BG_MAIN);

  lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);

  /*
   * Material You Button
   */

  lv_style_init(&style_btn);

  lv_style_set_radius(&style_btn, 24);

  lv_style_set_bg_color(&style_btn, S_COLOR_PRIMARY_CONTAINER);

  lv_style_set_text_color(&style_btn, S_COLOR_ON_PRIMARY_CONTAINER);

  lv_style_set_pad_hor(&style_btn, 24);

  lv_style_set_pad_ver(&style_btn, 12);

  lv_style_set_border_width(&style_btn, 0);

  /*
   * Press状态
   */

  lv_style_init(&style_btn_pressed);

  lv_style_set_bg_color(&style_btn_pressed, S_COLOR_PRIMARY);

  lv_style_set_text_color(&style_btn_pressed, S_COLOR_ON_PRIMARY);

  /*
   * Card
   */

  lv_style_init(&style_card);

  lv_style_set_radius(&style_card, 20);

  lv_style_set_bg_color(&style_card, S_COLOR_SURFACE_CONTAINER_HIGH);

  lv_style_set_pad_all(&style_card, 16);

  lv_style_set_border_width(&style_card, 0);

  /*
   * Label
   */

  lv_style_init(&style_label);

  lv_style_set_text_color(&style_label, S_TEXT_PRIMARY);

  lv_style_set_text_font(&style_label, &chili_cn);

  /*
   * install theme
   */

  lv_display_t *disp = lv_display_get_default();

  if (!disp)
    return;

  lv_theme_t *theme = lv_theme_create();

  lv_theme_set_apply_cb(theme, my_theme_apply_cb);

  lv_theme_t *old = lv_display_get_theme(disp);

  lv_theme_set_parent(theme, old);

  lv_display_set_theme(disp, theme);

  lv_obj_add_style(lv_screen_active(), &style_screen, 0);
}