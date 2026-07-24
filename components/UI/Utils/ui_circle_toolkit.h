#ifndef UI_CIRCLE_TOOLKIT_H
#define UI_CIRCLE_TOOLKIT_H

#include <math.h>
#include <stdint.h>

#define UI_SCREEN_WIDTH 360
#define UI_SCREEN_HEIGHT 360
#define UI_SCREEN_RADIUS 180

/**
 * @brief 编译期静态宏：根据给定的顶部 y 坐标和控件高度 h，计算控件最突出的那个
 * Y 轴点所需的安全边距
 * @param y 控件顶部 Y 坐标 (0 ~ 360)
 * @param h 控件高度 (px)
 */
#define UI_CIRCLE_SAFE_PAD_Y(y, h)                                             \
  ((y) + (h) / 2 <= UI_SCREEN_RADIUS                                           \
       ? (180 - (int)sqrt(180 * 180 - (180 - (y)) * (180 - (y))))              \
       : (180 - (int)sqrt(180 * 180 - ((y) + (h) - 180) * ((y) + (h) - 180))))

/**
 * @brief 动态计算指定 Y 轴范围 [y_top, y_top + height]
 * 的完全不被圆屏裁切的左/右边距
 * @param y_top 控件顶部 Y 坐标
 * @param height 控件高度
 * @return 离外接正方形边缘的最小安全像素值
 */
static inline int16_t ui_circle_get_safe_pad(int16_t y_top, int16_t height) {
  if (y_top < 0)
    y_top = 0;
  int16_t y_bot = y_top + height;
  if (y_bot > UI_SCREEN_HEIGHT)
    y_bot = UI_SCREEN_HEIGHT;

  // 寻找离圆心 (Y=180) 最远端（即裁切最严重的端点）
  int16_t dy_top = (y_top > UI_SCREEN_RADIUS) ? (y_top - UI_SCREEN_RADIUS)
                                              : (UI_SCREEN_RADIUS - y_top);
  int16_t dy_bot = (y_bot > UI_SCREEN_RADIUS) ? (y_bot - UI_SCREEN_RADIUS)
                                              : (UI_SCREEN_RADIUS - y_bot);
  int16_t max_dy = (dy_top > dy_bot) ? dy_top : dy_bot;

  if (max_dy >= UI_SCREEN_RADIUS)
    return UI_SCREEN_RADIUS;

  // 勾股定理求解圆内可容纳半宽: x = sqrt(R^2 - dy^2)
  // 边距 pad = R - x
  float x =
      sqrtf((float)(UI_SCREEN_RADIUS * UI_SCREEN_RADIUS - max_dy * max_dy));
  return (int16_t)(UI_SCREEN_RADIUS - (int16_t)x);
}

/**
 * @brief 动态计算在 Y 轴范围内，圆形屏幕中可利用的最大安全宽度
 */
static inline int16_t ui_circle_get_safe_width(int16_t y_top, int16_t height) {
  int16_t pad = ui_circle_get_safe_pad(y_top, height);
  return UI_SCREEN_WIDTH - (pad * 2);
}

#endif /* UI_CIRCLE_TOOLKIT_H */