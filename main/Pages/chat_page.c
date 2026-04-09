#include "chat_comp.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "net_adapter.h"

static const char *TAG = "GS_CHAT_PAGE";

// 页面私有上下文
typedef struct {
  net_config_t net_cfg;
} chat_page_ctx_t;

/**
 * @brief 页面初始化：配置网络参数
 */
static void *chat_init_cb(void *args) {
  chat_page_ctx_t *ctx = calloc(1, sizeof(chat_page_ctx_t));

  if (!ctx)
    return NULL;

  ctx->net_cfg.host = "10.113.233.106";
  ctx->net_cfg.port = 3000;
  ctx->net_cfg.user_id = "1";
  ctx->net_cfg.device_id = "3";

  //   // 触发底层连接
  //   net_ws_connect(&ctx->net_cfg);

  return ctx;
}

/**
 * @brief 页面渲染：调用你现有的界面创建函数
 */
static lv_obj_t *chat_render_cb(lv_obj_t *parent, void *ctx) {
  return chat_comp_create(parent);
}

/**
 * @brief 页面轮询：由 gs_nav_loop 定期触发
 */
static void chat_update_cb(void *ctx) {
  if (!ctx)
    return;

  if (!net_is_connected()) {
    chat_page_ctx_t *c = (chat_page_ctx_t *)ctx;
    net_ws_connect(&c->net_cfg);
  }
  chat_comp_loop();
}

/**
 * @brief 页面销毁：断开连接并释放内存
 */
static void chat_deinit_cb(void *ctx) {
  net_ws_disconnect();
  free(ctx);
  ESP_LOGI(TAG, "Chat page destroyed, connection closed.");
}

// 导出页面描述符
const gs_page_desc_t page_chat = {.init_cb = chat_init_cb,
                                  .render_cb = chat_render_cb,
                                  .update_cb = chat_update_cb,
                                  .deinit_cb = chat_deinit_cb};