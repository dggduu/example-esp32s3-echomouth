#ifndef GS_NAV_H
#define GS_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 页面生命周期描述符（同步版）
 */
typedef struct {
  // 初始化：args 为传入参数，返回私有上下文（可为 NULL）
  void *(*init_cb)(void *args);
  // 渲染：parent 为容器，ctx 为 init_cb 返回的私有数据，返回页面对象
  lv_obj_t *(*render_cb)(lv_obj_t *parent, void *ctx);
  // 销毁：释放 ctx 中所有资源
  void (*deinit_cb)(void *ctx);
} gs_page_desc_t;

/**
 * @brief 初始化导航系统
 * @param container 页面容器（必填，所有页面将创建在此容器内）
 */
void gs_nav_init(lv_obj_t *container);

/**
 * @brief 压入新页面（同步，会立即切换）
 * @param page 页面描述符
 * @param args 传递给 init_cb 的参数
 * @return 0 成功，-1 失败（栈满或状态错误）
 */
int gs_nav_push(const gs_page_desc_t *page, void *args);

/**
 * @brief 弹出当前页面，返回上一页（同步）
 * @return 0 成功，-1 栈空
 */
int gs_nav_pop(void);

/**
 * @brief 获取当前页面栈深度
 */
int gs_nav_depth(void);

#ifdef __cplusplus
}
#endif

#endif