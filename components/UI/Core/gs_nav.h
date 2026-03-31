#ifndef GS_NAV_H
#define GS_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 页面生命周期描述符
 */
typedef struct {
  // 初始化：args 为传入参数，返回私有上下文（可为 NULL）
  void *(*init_cb)(void *args);
  // 渲染：parent 为容器，ctx 为 init_cb 返回的私有数据，返回页面对象根节点
  lv_obj_t *(*render_cb)(lv_obj_t *parent, void *ctx);
  // 状态机更新：由 gs_nav_loop 定期触发，用于处理页面内部逻辑（如时间刷新）
  void (*update_cb)(void *ctx);
  // 销毁：释放 ctx 中所有资源
  void (*deinit_cb)(void *ctx);
} gs_page_desc_t;

/**
 * @brief 初始化导航系统
 * @param container 页面容器（所有页面将创建在此容器内）
 */
void gs_nav_init(lv_obj_t *container);

/**
 * @brief 压入新页面
 * @param page 页面描述符
 * @param args 传递给 init_cb 的参数
 * @return 0 成功，-1 失败
 */
int gs_nav_push(const gs_page_desc_t *page, void *args);

/**
 * @brief 弹出当前页面，返回上一页
 * @return 0 成功，-1 栈空
 */
int gs_nav_pop(void);

/**
 * @brief 导航系统逻辑轮询（核心）
 * @note 必须在 UI 任务主循环中调用，用于触发当前页面的 update_cb
 */
void gs_nav_loop(void);

/**
 * @brief 获取当前页面栈深度
 */
int gs_nav_depth(void);

/**
 * @brief 异步压入新页面
 * @note 线程安全，可在任意任务中调用
 */
void gs_nav_push_async(const gs_page_desc_t *page, void *args);

/**
 * @brief 异步弹出页面
 */
void gs_nav_pop_async(void);

#ifdef __cplusplus
}
#endif

#endif